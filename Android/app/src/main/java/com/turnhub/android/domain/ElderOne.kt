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

    /** A new match: the same Elder One and episode, back at the start of the track. */
    fun fresh(): ElderOneTrack = ElderOneTrack(elderOne = elderOne, episode = episode)

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

        fun stageName(stage: Int): String = if (stage >= FINAL_STAGE) "Final stage" else "Stage $stage"

        private fun clean(name: String?): String? = name?.trim()?.take(MAX_NAME)?.takeIf { it.isNotEmpty() }
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
    val elderOnes: List<StoryGroup> = listOf(
        StoryGroup("Season 1 (core box)", listOf("Cthulhu", "Hastur")),
        StoryGroup("Unspeakable Box", listOf("Yog-Sothoth", "Dagon")),
        StoryGroup("Black Goat of the Woods", listOf("Shub-Niggurath")),
        StoryGroup("Wendigo Box", listOf("Ithaqua")),
        StoryGroup("Season 3 (Fear of the Unknown)", listOf("Azathoth", "Tsathoggua")),
        StoryGroup("Unknowable Box", listOf("Nug & Yeb")),
        StoryGroup("Unimaginable Box", listOf("Xylarthoth", "Cthugha")),
        StoryGroup("Season 4", listOf("Nyarlathotep")),
        StoryGroup("Season 5", listOf("Yig", "Ghatanothoa")),
        StoryGroup("Season 6", listOf("Abhoth", "Atlach-Nacha")),
        StoryGroup("Epic and special", listOf("R'lyeh Rising", "Godzilla")),
    )

    val episodes: List<StoryGroup> = listOf(
        StoryGroup("Season 1", listOf("Blasphemous Alchemy", "Tomes of Madness", "Danse Macabre", "Eldritch Idols",
            "Accursed Tide", "Unspeakable Hour")),
        StoryGroup("Season 2", listOf("Strange Bedfellows", "Nameless Woods", "Sanctificetur Nomen", "Creeping Time",
            "Monster in Sin City", "Endgame")),
        StoryGroup("Season 3 (Fear of the Unknown)", listOf("Top of the Mourning", "Isles of the Damned", "Deliver Us from Evil",
            "By Request of the Queen", "Festival of Death", "Die Schöne und das Biest")),
        StoryGroup("Season 4", listOf("Horror in the Sky", "The Haunted Garden", "Fields of Hell", "Death on the Nile",
            "Pyramid of Fear", "The Window on the Infinite")),
        StoryGroup("Season 5", listOf("At the Mountains of Madness", "Beneath the Waves", "The Nameless City",
            "The Lost Continent", "Imprisoned with the Pharaohs")),
        StoryGroup("Lost Episodes (Unspeakable Box)", listOf("Speak Fhtagn or Die!", "Bright Lights, Big Monsters", "Happy Birthday")),
        StoryGroup("Lost Episodes (Unknowable Box)", listOf("Hypnos", "Dreams in the Witch House", "The Colour Out of Space",
            "Horror in the Museum")),
        StoryGroup("Lost Episodes (Unimaginable Box)", listOf("The Rats in the Walls", "The Case of Charles Dexter Ward",
            "Reanimator", "Through the Gates of the Silver Key")),
        StoryGroup("Epic Episodes", listOf("R'lyeh Rising", "Godzilla Rising", "Godzilla vs. Cthulhu")),
    )
}
