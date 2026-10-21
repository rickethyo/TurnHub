package com.turnhub.android.domain

/**
 * Cthulhu: Death May Die's Elder One and episode, as the Story board tracks them
 * (CMON rulebook: "Story board", "Check the Mythos discard pile", "Check the Elder
 * One summoning", "Fighting the Elder One", "Ending the game").
 *
 * The Summoning track has 8 spaces: the Elder One starts on space 1 and is summoned
 * when it enters the first red space ([FIRST_RED_SPACE]) or when the ritual is
 * disrupted. From then on the Progression token moves instead, and reaching the last
 * space ([LAST_SPACE]) loses the game. Three Summoning symbols in the Mythos discard
 * pile advance it one space (the discards are then reshuffled).
 *
 * Stage 1 is on top until the Elder One is summoned, which reveals Stage 2. Stages 2,
 * 3 and Final ([FINAL_STAGE]) each have printed health; the Elder One can only be
 * wounded once the ritual is disrupted, a stage falls when its wounds reach its health,
 * excess wounds don't carry over, and defeating the Final stage wins.
 *
 * Immutable: each change returns the next track, or the same one when it doesn't apply.
 * Effects stay on the physical cards; this only remembers where things stand.
 */
data class ElderOneTrack(
    val elderOne: String? = null,
    val episode: String? = null,
    /** Summoning track space, [START_SPACE] to [LAST_SPACE]: the figure, then the Progression token. */
    val space: Int = START_SPACE,
    /** On the board (the Stage 2 card or later is revealed). */
    val summoned: Boolean = false,
    /** The episode's task is done: the Elder One can be attacked. */
    val ritualDisrupted: Boolean = false,
    /** The Stage card on top: 1 while it is on the track, then 2, 3 and [FINAL_STAGE]. */
    val stage: Int = 1,
    /** Wounds on the current stage. */
    val stageWounds: Int = 0,
    /** Health printed on stages 2, 3 and Final, as the table enters it. */
    val stageHealth: Map<Int, Int> = emptyMap(),
    /** Summoning symbols in the Mythos discard pile since the last advance (0 to 2). */
    val symbols: Int = 0,
    /** The episode's own tokens (gates, seals and so on), named by the table, in the order added. */
    val tokens: List<EpisodeToken> = emptyList(),
    /** Minions and episode monsters on the map, in the order they arrived. */
    val enemies: List<EnemyFigure> = emptyList(),
) {
    /** The ritual is disrupted and the Elder One is on the board: wounds count. */
    val vulnerable: Boolean get() = summoned && ritualDisrupted

    /** The Progression token reached the last space: the investigators lose. */
    val dominates: Boolean get() = space >= LAST_SPACE

    /** The printed health of the current stage, once the table has entered it. */
    val health: Int? get() = stageHealth[stage]

    /** The current stage has taken at least its health in wounds. */
    val stageBeaten: Boolean get() = vulnerable && health?.let { stageWounds >= it } == true

    /** Something has been chosen or moved, so the table is tracking the Elder One here. */
    val inUse: Boolean get() = elderOne != null || episode != null || space != START_SPACE || summoned || ritualDisrupted || symbols > 0

    fun withElderOne(name: String?): ElderOneTrack = copy(elderOne = clean(name))

    fun withEpisode(name: String?): ElderOneTrack = copy(episode = clean(name))

    /**
     * A Mythos card with a Summoning symbol was discarded ([delta] 1) or one was
     * miscounted (-1). The third symbol advances the Elder One and empties the count,
     * as the discards are reshuffled.
     */
    fun symbol(delta: Int): ElderOneTrack {
        if (delta !in -1..1 || delta == 0 || dominates) return this
        val next = symbols + delta
        return when {
            next < 0 -> this
            next >= SYMBOLS_TO_ADVANCE -> copy(symbols = 0).advance(1)
            else -> copy(symbols = next)
        }
    }

    /** Moves the Elder One (or the Progression token) [delta] spaces; entering a red space summons it. */
    fun advance(delta: Int): ElderOneTrack {
        if (delta !in -1..1 || delta == 0) return this
        val next = (space + delta).coerceIn(START_SPACE, LAST_SPACE)
        if (next == space) return this
        val moved = copy(space = next)
        return if (delta > 0 && next >= FIRST_RED_SPACE) moved.summon() else moved
    }

    /** The Elder One comes into our world: Stage 1 moves aside, revealing Stage 2. */
    fun summon(): ElderOneTrack = if (summoned) this else copy(summoned = true, stage = maxOf(stage, 2), stageWounds = 0)

    /** A correction: the Elder One goes back on the track before any stage has fallen. */
    fun unsummon(): ElderOneTrack =
        if (!summoned || stage != 2 || ritualDisrupted) this else copy(summoned = false, stage = 1, stageWounds = 0)

    /** The episode's task is done; an Elder One still on the track is summoned at the end of the turn. */
    fun disrupt(): ElderOneTrack = if (ritualDisrupted) this else copy(ritualDisrupted = true).summon()

    /** A correction: the ritual isn't disrupted after all (no wounds have landed on this stage). */
    fun undisrupt(): ElderOneTrack = if (!ritualDisrupted || stageWounds > 0) this else copy(ritualDisrupted = false)

    /** Wounds on the current stage; only once it can be attacked. */
    fun wound(delta: Int): ElderOneTrack {
        if (!vulnerable || delta == 0) return this
        val next = (stageWounds + delta).coerceIn(0, MAX_WOUNDS)
        return if (next == stageWounds) this else copy(stageWounds = next)
    }

    /** The health printed on the current stage. */
    fun withHealth(health: Int?): ElderOneTrack {
        if (stage < 2) return this
        if (health != null && health !in 1..MAX_WOUNDS) return this
        return copy(stageHealth = if (health == null) stageHealth - stage else stageHealth + (stage to health))
    }

    /**
     * The current stage is defeated: the next is revealed with no wounds (excess doesn't
     * carry over). The Final stage has no next: the caller ends the game as a win.
     */
    fun defeatStage(): ElderOneTrack = if (!vulnerable || stage >= FINAL_STAGE) this else copy(stage = stage + 1, stageWounds = 0)

    /** A new match: the same Elder One, episode and token names, back at the start of the track with no tokens out. */
    fun fresh(): ElderOneTrack = ElderOneTrack(elderOne = elderOne, episode = episode, tokens = tokens.map { it.copy(count = 0) })

    /** A new kind of episode token; a blank, repeated (any case) or one-too-many name changes nothing. */
    fun withToken(name: String): ElderOneTrack {
        val clean = clean(name) ?: return this
        if (tokens.size >= MAX_TOKENS || tokens.any { it.name.equals(clean, ignoreCase = true) }) return this
        return copy(tokens = tokens + EpisodeToken(clean))
    }

    fun withoutToken(index: Int): ElderOneTrack =
        if (index !in tokens.indices) this else copy(tokens = tokens.filterIndexed { i, _ -> i != index })

    /** A figure of [card] arrives, numbered after the others of its kind on the map. */
    fun withEnemy(card: EnemyCard): ElderOneTrack {
        val clean = card.normalized() ?: return this
        if (enemies.size >= MAX_ENEMIES) return this
        val taken = enemies.filter { it.card.name.equals(clean.name, ignoreCase = true) }.map { it.number }.toSet()
        return copy(enemies = enemies + EnemyFigure(clean, generateSequence(1) { it + 1 }.first { it !in taken }))
    }

    /** Wounds on figure [index]; it stays on the map at its health until the table removes it. */
    fun woundEnemy(index: Int, delta: Int): ElderOneTrack {
        val figure = enemies.getOrNull(index) ?: return this
        val next = (figure.wounds + delta).coerceIn(0, figure.card.health)
        return if (next == figure.wounds) this else copy(enemies = enemies.mapIndexed { i, f -> if (i == index) f.copy(wounds = next) else f })
    }

    /** The figure dies or leaves the map. */
    fun withoutEnemy(index: Int): ElderOneTrack =
        if (index !in enemies.indices) this else copy(enemies = enemies.filterIndexed { i, _ -> i != index })

    /** Tokens of kind [index] out on the board, from 0 to [MAX_TOKEN_COUNT]. */
    fun token(index: Int, delta: Int): ElderOneTrack {
        val token = tokens.getOrNull(index) ?: return this
        val next = (token.count + delta).coerceIn(0, MAX_TOKEN_COUNT)
        return if (next == token.count) this
        else copy(tokens = tokens.mapIndexed { i, t -> if (i == index) t.copy(count = next) else t })
    }

    /** One line for the menu bar and Game tab, e.g. "Hastur · Stage 2 · 4/12 wounds". */
    fun headline(): String = listOfNotNull(
        elderOne ?: "Elder One",
        if (!summoned) "Summoning track $space/$LAST_SPACE" else stageName(stage),
        if (vulnerable) "$stageWounds${health?.let { "/$it" } ?: ""} wounds" else null,
    ).joinToString(" · ")

    companion object {
        const val START_SPACE = 1
        const val FIRST_RED_SPACE = 5
        const val LAST_SPACE = 8
        const val FINAL_STAGE = 4
        const val SYMBOLS_TO_ADVANCE = 3
        const val MAX_WOUNDS = 99
        const val MAX_NAME = 40
        const val MAX_TOKENS = 6
        const val MAX_TOKEN_COUNT = 99
        const val MAX_ENEMIES = 30
        /** Names to start from; the table can type any other. */
        val SUGGESTED_TOKENS = listOf("Gate", "Seal", "Elder Sign", "Clue", "Ritual")

        fun stageName(stage: Int): String = if (stage >= FINAL_STAGE) "Final stage" else "Stage $stage"

        internal fun clean(name: String?): String? = name?.trim()?.take(MAX_NAME)?.takeIf { it.isNotEmpty() }
    }
}

/** One box's Elder Ones or one season's episodes, for the Elder One tab's pickers. */
data class StoryGroup(val title: String, val names: List<String>)

/**
 * The Elder Ones and episodes by box, compiled from the community Death May Die wiki
 * (cthulhu-death-may-die.fandom.com, "Elder Ones" and "Episodes"); spelling follows the
 * printed titles where the wiki has typos. Names only, and any name can be typed in.
 */
object DeathMayDieStory {
    val elderOnes: List<StoryGroup> = DmdContent.elderOnes

    val episodes: List<StoryGroup> = DmdContent.episodes
}

/** One kind of episode token and how many are out. */
data class EpisodeToken(val name: String, val count: Int = 0)
