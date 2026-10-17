package com.turnhub.android.standalone

import com.turnhub.android.domain.ControllerHandle
import com.turnhub.android.domain.StarCommander
import com.turnhub.android.domain.TablePlayer
import com.turnhub.android.domain.TableSummary
import com.turnhub.android.domain.LocalTimerRules
import com.turnhub.android.domain.SpecialCondition
import com.turnhub.android.domain.TurnAction
import com.turnhub.android.domain.TurnStatus
import com.turnhub.android.protocol.CommanderDamage
import com.turnhub.android.protocol.PlayerCounters
import com.turnhub.android.protocol.GameProfile
import com.turnhub.android.protocol.PendingDecisions
import com.turnhub.android.protocol.TableSettings
import com.turnhub.android.protocol.TableState
import com.turnhub.android.protocol.TurnTimer
import com.turnhub.android.protocol.TurnTimerPhase
import kotlin.random.Random

/**
 * One player at the local table. [localId] keys reusable device-local identity;
 * names are snapshots, never identity. Legacy [profileId] is unscoped and must
 * not be used for delivery without an explicit per-match Atlas mapping.
 */
data class LocalPlayer(
    val name: String,
    val profileId: String? = null,
    val life: Int = 0,
    val eliminated: Boolean = false,
    val conceded: Boolean = false,
    val counters: PlayerCounters = PlayerCounters(),
    /** 1 for the first player out, 2 for the next, and so on. */
    val outOrder: Int? = null,
    val turnsCompleted: Int = 0,
    /** Time spent on completed turns, and the quickest and longest of them (0 until one completes). */
    val turnMs: Long = 0,
    val fastestTurnMs: Long = 0,
    val longestTurnMs: Long = 0,
    /** Commander damage received, by source player index: one total per commander (1 and 2). */
    val commanderDamage: Map<Int, List<Int>> = emptyMap(),
    val localId: String? = null,
    val demerits: Int = 0,
    val extensionsEarned: Int = 0,
    val extensionsUsed: Int = 0,
    /** The commander this player brought, for the table's banter; shown, never scored. */
    val commander: String? = null,
    /** The Scryfall printing whose art [commander] shows; null uses Scryfall's default printing. */
    val commanderArt: String? = null,
    /** [commander] is a card for the tile only (picked outside Commander, or at random), not a commander. */
    val cardOnly: Boolean = false,
    /** Pokémon and Lorcana: this turn's once-per-turn actions; Pokémon's Special Conditions. */
    val turn: TurnStatus = TurnStatus(),
)

/**
 * The standalone tablet game (Android/README.md "The one exception"): with no
 * Atlas at the table, the app keeps its own simple game of life, Commander
 * damage, turns and a winner. It never merges into a game Atlas runs; once it
 * ends it becomes a [GameRecord] that Atlas imports on the next connect.
 *
 * Immutable: every action returns the next game, or the same one when the
 * action doesn't apply. Clocks use wall time ([nowMs]) so a game survives the
 * app being closed; pausing stops them.
 */
data class StandaloneGame(
    /** Fresh for every match (start, rematch); the record's dedupe key on Atlas. */
    val gameId: String = "",
    val state: TableState = TableState.LOBBY,
    val profile: GameProfile = GameProfile.MTG_COMMANDER,
    val startingLife: Int = 40,
    val autoEliminateAtZeroLife: Boolean = true,
    val autoEliminateAtPoisonLimit: Boolean = true,
    val autoEliminateAtCommanderDamage: Boolean = true,
    val poisonLimit: Int = 10,
    /** 0 = off; else the base countdown each turn gets. */
    val turnTimerMs: Long = 0,
    val timerRules: LocalTimerRules = LocalTimerRules(),
    val turnExtensionMs: Long = 0,
    val timeoutHandled: Boolean = false,
    /**
     * Two-Headed Giant (Magic and Commander), as on Atlas (GAMEPLAY.md): neighbours
     * in turn order (players 1+2, 3+4, ...) are teams sharing one life total,
     * poison count and turn, and leaving together.
     */
    val twoHeadedGiant: Boolean = false,
    /**
     * Official modes ([officialRules]) keep the official starting life, poison
     * limit and player count unless the table turns on house rules.
     */
    val houseRules: Boolean = false,
    val players: List<LocalPlayer> = emptyList(),
    /** Index into [players] of whose turn it is. */
    val active: Int? = null,
    val starter: Int? = null,
    /** The first of [winners]; null for a draw or while the game runs. */
    val winner: Int? = null,
    /** Every winner: one, except a shared Star Commander victory. */
    val winners: List<Int> = listOfNotNull(winner),
    /** One individual player, even when teams share life. */
    val monarch: Int? = null,
    val initiative: Int? = null,
    /** Wall clock when the match started. */
    val startedAtMs: Long = 0,
    /** Game time banked before [runningSinceMs]. */
    val elapsedMs: Long = 0,
    /** Wall clock the clocks last resumed at; null while they are stopped. */
    val runningSinceMs: Long? = null,
    /** Game time at which the current turn began. */
    val turnStartedAtElapsedMs: Long = 0,
    /** Planechase: the planar deck, die and counters ([Planechase]); off unless the table turns it on. */
    val planechase: Planechase = Planechase(),
    /** Pokémon: what the last Pokémon Checkup did, shown until the table dismisses it; empty when nothing happened. */
    val pokemonCheckup: List<String> = emptyList(),
    val revision: Long = 0,
) {
    fun gameElapsedMs(nowMs: Long): Long = elapsedMs + (runningSinceMs?.let { (nowMs - it).coerceAtLeast(0) } ?: 0)

    private fun bump(): StandaloneGame = copy(revision = revision + 1)

    private val playing: Boolean get() = state == TableState.RUNNING || state == TableState.PAUSED

    private val magic: Boolean get() = profile.magicRules

    private val star: Boolean get() = profile.starCommander

    private val pokemon: Boolean get() = profile.pokemon

    private val lorcana: Boolean get() = profile.lorcana

    /** [index] and, in Two-Headed Giant, their teammate. */
    fun mates(index: Int): List<Int> =
        if (!twoHeadedGiant) listOf(index) else (index - index % 2).let { listOf(it, it + 1) }.filter { it in players.indices }

    /** The official rules this game follows; null for a custom mode or house rules. */
    val locked: OfficialRules? get() = if (houseRules) null else officialRules(profile, twoHeadedGiant)

    /**
     * Two players, (Two-Headed Giant) an even table of at least four, or (Star Commander)
     * exactly five; an official mode's exact count (two teams of two, a two-player Duel).
     */
    val canStart: Boolean get() = when {
        star -> players.size == StarCommander.PLAYERS
        locked?.players != null -> players.size == locked?.players
        else -> players.size >= 2 && (!twoHeadedGiant || (players.size >= 4 && players.size % 2 == 0))
    }

    // --- the lobby ------------------------------------------------------------------

    fun addPlayer(name: String, profileId: String? = null, localId: String? = null): StandaloneGame {
        val trimmed = name.trim().take(MAX_NAME)
        if (state != TableState.LOBBY || trimmed.isEmpty() || players.size >= MAX_PLAYERS) return this
        if (profileId != null && players.any { it.profileId == profileId }) return this
        if (localId != null && players.any { it.localId == localId }) return this
        return copy(players = players + LocalPlayer(trimmed, profileId, localId = localId)).bump()
    }

    fun removePlayer(index: Int): StandaloneGame {
        if (state != TableState.LOBBY || index !in players.indices) return this
        val chosen = starter?.let { if (it == index) null else if (it > index) it - 1 else it }
        return copy(players = players.filterIndexed { i, _ -> i != index }, starter = chosen).bump()
    }

    /** Clears the selected roster while retaining the lobby's game settings. */
    fun clearPlayers(): StandaloneGame {
        if (state != TableState.LOBBY || players.isEmpty()) return this
        return copy(players = emptyList(), starter = null).bump()
    }

    /** Moves a player one place earlier ([by] = -1) or later (+1) in turn order. */
    fun movePlayer(index: Int, by: Int): StandaloneGame = swapPlayers(index, index + by)

    /** Trades two players' seats (and so their places in turn order); lobby only. */
    fun swapPlayers(a: Int, b: Int): StandaloneGame {
        if (state != TableState.LOBBY || a !in players.indices || b !in players.indices || a == b) return this
        val list = players.toMutableList()
        list[a] = players[b]
        list[b] = players[a]
        // The chosen starter is a person: they keep it in their new seat.
        val chosen = when (starter) { a -> b; b -> a; else -> starter }
        return copy(players = list, starter = chosen).bump()
    }

    /**
     * In the lobby [starter] is the player chosen to go first (null: random).
     * Choosing the same player again clears the choice. In Two-Headed Giant
     * their team goes first.
     */
    fun chooseStarter(index: Int?): StandaloneGame {
        if (state != TableState.LOBBY || (index != null && index !in players.indices)) return this
        return copy(starter = if (index == starter) null else index).bump()
    }

    fun setFormat(profile: GameProfile, startingLife: Int? = null): StandaloneGame {
        if (state != TableState.LOBBY) return this
        // Star Commander has no teams: choosing it turns Two-Headed Giant off.
        val teams = twoHeadedGiant && profile.magicRules && !profile.starCommander
        return copy(
            profile = profile,
            twoHeadedGiant = teams,
            poisonLimit = if (teams) TEAM_POISON else poisonLimit.takeIf { twoHeadedGiant == teams } ?: SOLO_POISON,
            startingLife = (startingLife ?: defaultLife(profile, teams)).coerceIn(1, MAX_LIFE),
            planechase = if (profile.magicRules) planechase else planechase.copy(enabled = false),
        ).officialValues().bump()
    }

    /** Puts an official mode's starting life and poison limit back, unless house rules are on. */
    private fun officialValues(): StandaloneGame = locked?.let { rules ->
        copy(startingLife = rules.life, poisonLimit = rules.poison ?: poisonLimit)
    } ?: this

    /** House rules unlock an official mode's life and limits; turning them off restores the official values. */
    fun setHouseRules(on: Boolean): StandaloneGame {
        if (state != TableState.LOBBY || on == houseRules) return this
        return copy(houseRules = on).officialValues().bump()
    }

    /** The game mode: Two-Headed Giant (Magic and Commander, not Star Commander) or standard; lobby only. */
    fun setTwoHeadedGiant(on: Boolean): StandaloneGame {
        if (state != TableState.LOBBY || on == twoHeadedGiant || (on && (!magic || star))) return this
        return copy(
            twoHeadedGiant = on,
            startingLife = defaultLife(profile, on),
            poisonLimit = if (on) TEAM_POISON else SOLO_POISON,
        ).officialValues().bump()
    }

    fun setStartingLife(life: Int): StandaloneGame {
        if (state != TableState.LOBBY || life !in 1..MAX_LIFE || locked != null) return this
        return copy(startingLife = life).bump()
    }

    /** Off (0) or 15 s to 60 min in whole seconds, Atlas's range; set in the lobby only. */
    fun setTurnTimer(ms: Long): StandaloneGame {
        if (state != TableState.LOBBY || (ms != 0L && (ms !in TURN_TIMER_RANGE_MS || ms % 1000 != 0L))) return this
        return copy(turnTimerMs = ms).bump()
    }

    fun setTimerRules(rules: LocalTimerRules): StandaloneGame =
        if (state != TableState.LOBBY || timerRules == rules) this else copy(timerRules = rules).bump()

    /** Settle only the current expired turn; returning to the app never skips a chain of turns. */
    fun applyTurnTimeout(nowMs: Long): StandaloneGame {
        val holder = active ?: return this
        if (state != TableState.RUNNING || turnTimerMs <= 0 || timeoutHandled ||
            gameElapsedMs(nowMs) - turnStartedAtElapsedMs < turnTimerMs + turnExtensionMs) return this
        val team = mates(holder)
        val settled = copy(timeoutHandled = true, players = players.mapIndexed { i, p ->
            if (timerRules.timeout.demerit && i in team && !p.eliminated) p.copy(demerits = p.demerits + 1) else p
        }).bump()
        return if (timerRules.timeout.autoPass) settled.advanceTurn(nowMs, reward = false) else settled
    }

    /** One banked extension per turn, used before expiry; teammates share the bank. */
    fun extendTurn(index: Int, nowMs: Long): StandaloneGame {
        val holder = active ?: return this
        val team = mates(holder)
        val player = players[holder]
        if (state != TableState.RUNNING || index !in team || turnTimerMs <= 0 || !timerRules.earnExtensions ||
            turnExtensionMs != 0L || timeoutHandled || player.extensionsEarned <= player.extensionsUsed ||
            gameElapsedMs(nowMs) - turnStartedAtElapsedMs >= turnTimerMs) return this
        return copy(turnExtensionMs = turnTimerMs / 2, players = players.mapIndexed { i, p ->
            if (i in team) p.copy(extensionsUsed = p.extensionsUsed + 1) else p
        }).bump()
    }

    /** The turn clock's phase, derived like Atlas's (GAMEPLAY.md "Turn timer"). */
    fun turnTimer(nowMs: Long): TurnTimer {
        if (active == null || !playing) return TurnTimer(TurnTimerPhase.NORMAL, null)
        val turn = (gameElapsedMs(nowMs) - turnStartedAtElapsedMs).coerceAtLeast(0)
        if (turnTimerMs <= 0) {
            return TurnTimer(if (turn >= LONG_TURN_MS) TurnTimerPhase.LONG_TURN else TurnTimerPhase.NORMAL, null)
        }
        val remaining = (turnTimerMs + turnExtensionMs - turn).coerceAtLeast(0)
        val phase = when {
            remaining == 0L -> TurnTimerPhase.EXPIRED
            remaining <= WARNING_MS -> TurnTimerPhase.WARNING
            else -> TurnTimerPhase.NORMAL
        }
        return TurnTimer(phase, remaining)
    }

    /** Starts a match with a random first player; needs two players. */
    fun start(nowMs: Long, gameId: String, random: Random = Random.Default, planarDeck: List<PlanarCard> = emptyList()): StandaloneGame {
        if (state != TableState.LOBBY && state != TableState.GAME_OVER) return this
        if (!canStart) return this
        // The lobby's chosen starter, else random (a rematch is random too). A team game
        // starts with a team: its first player holds the turn for both.
        val chosen = starter?.takeIf { state == TableState.LOBBY && it in players.indices }
        val first = when {
            chosen != null -> if (twoHeadedGiant) chosen - chosen % 2 else chosen
            twoHeadedGiant -> random.nextInt(players.size / 2) * 2
            else -> random.nextInt(players.size)
        }
        return copy(
            gameId = gameId,
            state = TableState.RUNNING,
            // Lorcana counts lore up from 0 to [startingLife].
            players = players.map { LocalPlayer(it.name, it.profileId, life = if (lorcana) 0 else startingLife, localId = it.localId, commander = it.commander, commanderArt = it.commanderArt, cardOnly = it.cardOnly) },
            active = first,
            starter = first,
            winner = null,
            winners = emptyList(),
            monarch = null,
            initiative = null,
            startedAtMs = nowMs,
            elapsedMs = 0,
            runningSinceMs = nowMs,
            turnStartedAtElapsedMs = 0,
            turnExtensionMs = 0,
            timeoutHandled = false,
            planechase = if (planechase.enabled && magic) planechase.dealt(planarDeck) else planechase.cleared(),
            pokemonCheckup = emptyList(),
        ).bump()
    }

    /** Back to the lobby with the same players, to change who plays. */
    fun reset(): StandaloneGame {
        if (state == TableState.LOBBY) return this
        return StandaloneGame(
            profile = profile,
            startingLife = startingLife,
            autoEliminateAtZeroLife = autoEliminateAtZeroLife,
            autoEliminateAtPoisonLimit = autoEliminateAtPoisonLimit,
            autoEliminateAtCommanderDamage = autoEliminateAtCommanderDamage,
            poisonLimit = poisonLimit,
            turnTimerMs = turnTimerMs,
            timerRules = timerRules,
            twoHeadedGiant = twoHeadedGiant,
            houseRules = houseRules,
            planechase = planechase.cleared(),
            players = players.map { LocalPlayer(it.name, it.profileId, localId = it.localId, commander = it.commander, commanderArt = it.commanderArt, cardOnly = it.cardOnly) },
            revision = revision + 1,
        )
    }

    /**
     * Undoes a start: within [UNDO_START_MS] of game time, back to the lobby with the
     * same players and settings. Nothing is recorded, as the game never finished.
     */
    fun backToSetup(nowMs: Long): StandaloneGame =
        if (!playing || gameElapsedMs(nowMs) > UNDO_START_MS) this else reset()

    // --- the table ------------------------------------------------------------------

    /** [random] flips Pokémon Checkup's coins. */
    fun pass(index: Int, nowMs: Long, random: Random = Random.Default): StandaloneGame {
        val holder = active ?: return this
        // Either teammate may pass the team's turn; it goes to the next team.
        val team = mates(holder)
        if (state != TableState.RUNNING || index !in team) return this
        val settled = applyTurnTimeout(nowMs)
        // A timeout may already have passed this turn; don't also pass the next one.
        if (settled.active != active) return settled
        return settled.advanceTurn(nowMs, reward = true, random = random)
    }

    private fun advanceTurn(nowMs: Long, reward: Boolean, random: Random = Random.Default): StandaloneGame {
        val holder = active ?: return this
        val team = mates(holder)
        val next = nextIn(team.last()) ?: return this
        val now = gameElapsedMs(nowMs)
        val took = (now - turnStartedAtElapsedMs).coerceAtLeast(0)
        val earn = reward && timerRules.earnExtensions && turnTimerMs > 0 && !timeoutHandled && turnExtensionMs == 0L &&
            players[holder].turnsCompleted >= 3 && took < turnTimerMs * timerRules.earnBeforePercent / 100 &&
            players[holder].extensionsEarned < timerRules.maxEarnedPerGame
        val (checked, checkup) = when {
            pokemon -> pokemonCheckup(holder, random)
            lorcana -> players.map { it.copy(turn = it.turn.copy(turnActions = emptySet())) } to emptyList()
            else -> players to emptyList()
        }
        return copy(
            pokemonCheckup = checkup,
            players = checked.mapIndexed { i, p ->
                if (i !in team || p.eliminated) p else p.copy(
                    turnsCompleted = p.turnsCompleted + 1,
                    turnMs = p.turnMs + took,
                    fastestTurnMs = if (p.fastestTurnMs == 0L) took else minOf(p.fastestTurnMs, took),
                    longestTurnMs = maxOf(p.longestTurnMs, took),
                    extensionsEarned = p.extensionsEarned + if (earn) 1 else 0,
                )
            },
            active = next,
            turnStartedAtElapsedMs = now,
            turnExtensionMs = 0,
            timeoutHandled = false,
            planechase = planechase.newTurn(),
        ).bump()
    }

    /** Pauses a running game, or resumes a paused one. */
    fun togglePause(nowMs: Long): StandaloneGame = when (state) {
        TableState.RUNNING -> copy(state = TableState.PAUSED, elapsedMs = gameElapsedMs(nowMs), runningSinceMs = null).bump()
        TableState.PAUSED -> copy(state = TableState.RUNNING, runningSinceMs = nowMs).bump()
        else -> this
    }

    fun changeLife(index: Int, delta: Int, nowMs: Long = startedAtMs): StandaloneGame {
        val player = players.getOrNull(index) ?: return this
        if (!playing || player.eliminated || delta == 0) return this
        // Pokémon counts Prize cards left: taking the last one wins.
        if (pokemon) {
            val prizes = (player.life + delta).coerceIn(0, maxOf(startingLife, player.life))
            if (prizes == player.life) return this
            val taken = update(index) { it.copy(life = prizes) }
            return if (prizes == 0) taken.finish(listOf(index), nowMs) else taken
        }
        // Lorcana counts lore up: reaching the goal wins at once.
        if (lorcana) {
            val lore = (player.life + delta).coerceIn(0, MAX_LIFE)
            if (lore == player.life) return this
            val gained = update(index) { it.copy(life = lore) }
            return if (lore >= startingLife) gained.finish(listOf(index), nowMs) else gained
        }
        val life = (player.life.toLong() + delta).coerceIn(-MAX_LIFE.toLong(), MAX_LIFE.toLong()).toInt()
        return updateTeam(index) { it.copy(life = life) }.resolveEliminationRules(nowMs)
    }

    /**
     * Commander damage [index] received from [source]'s [commander] (1 or 2).
     * It costs the same life, as on Atlas; a total never goes below zero. Star
     * Commander allies can't deal each other Commander damage.
     */
    fun commanderDamage(index: Int, source: Int, commander: Int, delta: Int, nowMs: Long = startedAtMs): StandaloneGame {
        val player = players.getOrNull(index) ?: return this
        if (!profile.commanderRules || !playing || player.eliminated || source == index || source !in players.indices || commander !in 1..2) return this
        if (star && StarCommander.allied(index, source)) return this
        val totals = player.commanderDamage[source] ?: listOf(0, 0)
        val now = totals[commander - 1]
        val next = (now + delta).coerceIn(0, MAX_LIFE)
        val applied = next - now
        if (applied == 0) return this
        val updated = totals.toMutableList().also { it[commander - 1] = next }
        // Damage is counted per player; in Two-Headed Giant it comes off the team's life.
        return update(index) { it.copy(commanderDamage = it.commanderDamage + (source to updated)) }
            .updateTeam(index) { it.copy(life = it.life - applied) }
            .resolveEliminationRules(nowMs)
    }

    /** The player leaves the game; when one is left standing, they win. */
    fun claimMonarch(index: Int): StandaloneGame {
        if (!playing || index !in players.indices || players[index].eliminated ||
            !magic || monarch == index) return this
        return copy(monarch = index).bump()
    }

    fun changeCounter(index: Int, kind: String, value: Int, commander: Int = 1, nowMs: Long = startedAtMs): StandaloneGame {
        val player = players.getOrNull(index) ?: return this
        if (pokemon || lorcana) return markTurn(index, kind, value)
        if (!playing || player.eliminated || !magic) return this
        if (kind == "initiative") {
            if (value !in 0..1 || (value == 0 && initiative != index)) return this
            return copy(initiative = if (value == 1) index else null).bump()
        }
        val c = player.counters
        if (kind == "city-blessing") {
            if (value !in 0..1) return this
            return update(index) { it.copy(counters = c.copy(cityBlessing = value == 1)) }
        }
        if (value == 0 || value !in -MAX_LIFE..MAX_LIFE) return this
        val old = when (kind) {
            "poison" -> c.poison
            "energy" -> c.energy
            "experience" -> c.experience
            "commander-casts" -> {
                if (!profile.commanderRules || commander !in 1..2) return this
                c.commanderCasts[commander-1]
            }
            else -> return this
        }
        val next = old + value
        if (next !in 0..MAX_LIFE) return this
        // A team shares its poison count.
        if (kind == "poison") return updateTeam(index) { it.copy(counters = it.counters.copy(poison = next)) }.resolveEliminationRules(nowMs)
        val updated = when (kind) {
            "poison" -> c.copy(poison = next)
            "energy" -> c.copy(energy = next)
            "experience" -> c.copy(experience = next)
            else -> c.copy(commanderCasts = c.commanderCasts.toMutableList().also { it[commander-1] = next })
        }
        return update(index) { it.copy(counters = updated) }.resolveEliminationRules(nowMs)
    }

    fun setEliminationRule(field: String, value: Int, nowMs: Long): StandaloneGame {
        if (state == TableState.GAME_OVER || (field != "poisonLimit" && value !in 0..1)) return this
        val next = when (field) {
            "autoEliminateAtZeroLife" -> copy(autoEliminateAtZeroLife = value == 1)
            "autoEliminateAtPoisonLimit" -> copy(autoEliminateAtPoisonLimit = value == 1)
            "autoEliminateAtCommanderDamage" -> copy(autoEliminateAtCommanderDamage = value == 1)
            "poisonLimit" -> if (value in 1..MAX_LIFE && locked == null) copy(poisonLimit = value) else return this
            else -> return this
        }
        return next.bump().resolveEliminationRules(nowMs)
    }

    fun concede(index: Int, nowMs: Long): StandaloneGame = eliminate(index, nowMs, conceded = true)

    fun eliminate(index: Int, nowMs: Long, conceded: Boolean = false): StandaloneGame {
        val player = players.getOrNull(index) ?: return this
        if (!playing || player.eliminated) return this
        // Teammates leave together.
        val leaving = mates(index).filter { !players[it].eliminated }
        val out = players.count { it.eliminated } + 1
        var game = copy(players = players.mapIndexed { i, p ->
            if (i in leaving) p.copy(eliminated = true, conceded = conceded, outOrder = out) else p
        }).bump().passStatuses(leaving)
        game.decided()?.let { return game.finish(it, nowMs) }
        if (active in leaving) {
            val next = game.nextIn(leaving.last()) ?: return game
            game = game.copy(active = next, turnStartedAtElapsedMs = game.gameElapsedMs(nowMs), turnExtensionMs = 0, timeoutHandled = false,
                planechase = game.planechase.newTurn())
        }
        return game
    }

    fun setAutoEliminateAtZeroLife(enabled: Boolean, nowMs: Long): StandaloneGame {
        if (state == TableState.GAME_OVER) return this
        return copy(autoEliminateAtZeroLife = enabled).bump().resolveEliminationRules(nowMs)
    }

    private fun resolveEliminationRules(nowMs: Long): StandaloneGame {
        if (!playing) return this
        val targets = players.indices.filter { i ->
            val p = players[i]
            !p.eliminated && ((autoEliminateAtZeroLife && profile.countsLife && p.life <= 0) ||
                (magic && autoEliminateAtPoisonLimit && p.counters.poison >= poisonLimit) ||
                (profile.commanderRules && autoEliminateAtCommanderDamage && p.commanderDamage.values.any { damage -> damage.any { it >= 21 } }))
        }.flatMap { mates(it) }.distinct().filter { !players[it].eliminated }
        if (targets.isEmpty()) return this
        var out = players.count { it.eliminated }
        var game = copy(players = players.mapIndexed { i, p ->
            if (i in targets) p.copy(eliminated = true, conceded = false, outOrder = ++out) else p
        }).passStatuses(targets).bump()
        game.decided()?.let { return game.finish(it, nowMs) }
        if (active in targets) {
            game = game.copy(active = game.nextIn(mates(active!!).last()), turnStartedAtElapsedMs = game.gameElapsedMs(nowMs), turnExtensionMs = 0, timeoutHandled = false,
                planechase = game.planechase.newTurn())
        }
        return game
    }

    /** The table agreed this player won (the claim is armed on the panel, so one tap can't end a game). */
    fun claimWin(index: Int, nowMs: Long): StandaloneGame {
        val player = players.getOrNull(index) ?: return this
        if (!playing || player.eliminated) return this
        return finish(listOf(index), nowMs)
    }

    /** The table ends the game with no winner (it still counts as played). */
    fun endWithoutWinner(nowMs: Long): StandaloneGame = if (!playing) this else finish(emptyList(), nowMs)

    /** The same players play again; a new [gameId] makes it a separate record. */
    fun rematch(nowMs: Long, gameId: String, random: Random = Random.Default, planarDeck: List<PlanarCard> = emptyList()): StandaloneGame {
        if (state != TableState.GAME_OVER) return this
        return start(nowMs, gameId, random, planarDeck)
    }

    /**
     * Sean Mode (Star Commander only): a rematch reseated so every player's two
     * allies become their enemies. Taking every second seat (1, 3, 5, 2, 4) turns
     * the star inside out: neighbours end up across it, and the players across
     * it become the new neighbours.
     */
    fun seanModeRematch(nowMs: Long, gameId: String, random: Random = Random.Default, planarDeck: List<PlanarCard> = emptyList()): StandaloneGame {
        if (state != TableState.GAME_OVER || !star || players.size != StarCommander.PLAYERS) return this
        val reseated = SEAN_MODE_ORDER.map { players[it] }
        return copy(players = reseated, starter = null).start(nowMs, gameId, random, planarDeck)
    }

    // --- Pokémon and Lorcana -----------------------------------------------------------

    /**
     * Marks a once-per-turn action (`turn-action:ENERGY`, ...; the active player's,
     * this turn, for this game) or a Pokémon Special Condition (`pokemon-condition:POISONED`, ...)
     * on or off ([value] 1 or 0).
     */
    private fun markTurn(index: Int, kind: String, value: Int): StandaloneGame {
        val player = players[index]
        if (!playing || player.eliminated || value !in 0..1) return this
        val on = value == 1
        val status = player.turn
        val next = when {
            kind.startsWith(ACTION_KIND) -> {
                val action = TurnAction.of(profile).firstOrNull { it.name == kind.removePrefix(ACTION_KIND) } ?: return this
                if (active != index) return this
                status.copy(turnActions = if (on) status.turnActions + action else status.turnActions - action)
            }
            kind.startsWith(CONDITION_KIND) && pokemon -> {
                val condition = SpecialCondition.entries.firstOrNull { it.name == kind.removePrefix(CONDITION_KIND) } ?: return this
                status.withCondition(condition, on)
            }
            else -> return this
        }
        return if (next == status) this else update(index) { it.copy(turn = next) }
    }

    /** The table has read the last Pokémon Checkup. */
    fun dismissCheckup(): StandaloneGame = if (pokemonCheckup.isEmpty()) this else copy(pokemonCheckup = emptyList()).bump()

    /**
     * Pokémon Checkup between turns, in the rulebook's order: Poisoned and Burned
     * damage (players place the counters), a coin for Burned and for Asleep (heads
     * removes it), and Paralyzed ends for [ended], whose turn just ended. Every
     * player's once-per-turn actions start over. Returns the players and what to tell the table.
     */
    private fun pokemonCheckup(ended: Int, random: Random): Pair<List<LocalPlayer>, List<String>> {
        val lines = mutableListOf<String>()
        val checked = players.mapIndexed { i, p ->
            var status = p.turn.copy(turnActions = emptySet())
            if (p.eliminated) return@mapIndexed p.copy(turn = status)
            val c = status.conditions
            if (SpecialCondition.POISONED in c) lines += "${p.name}: Poisoned. Put 1 damage counter on the Active Pokémon."
            if (SpecialCondition.BURNED in c) {
                val heads = random.nextBoolean()
                lines += "${p.name}: Burned. Put 2 damage counters on the Active Pokémon. " +
                    if (heads) "Coin: heads, it's no longer Burned." else "Coin: tails, it's still Burned."
                if (heads) status = status.withCondition(SpecialCondition.BURNED, false)
            }
            if (SpecialCondition.ASLEEP in c) {
                val heads = random.nextBoolean()
                lines += "${p.name}: Asleep. " + if (heads) "Coin: heads, it wakes up." else "Coin: tails, it stays Asleep."
                if (heads) status = status.withCondition(SpecialCondition.ASLEEP, false)
            }
            if (SpecialCondition.PARALYZED in c && i == ended) {
                lines += "${p.name}: no longer Paralyzed."
                status = status.withCondition(SpecialCondition.PARALYZED, false)
            }
            p.copy(turn = status)
        }
        return checked to lines
    }

    // --- Planechase --------------------------------------------------------------------

    /** Turns Planechase on or off for the next game (Magic formats; lobby only). */
    fun setPlanechase(on: Boolean): StandaloneGame {
        if (state != TableState.LOBBY || on == planechase.enabled || (on && !magic)) return this
        return copy(planechase = planechase.copy(enabled = on)).bump()
    }

    /** A digital deck TurnHub shuffles, or the players' own cards; lobby only. */
    fun setPlanarSource(source: PlanarDeckSource): StandaloneGame {
        if (state != TableState.LOBBY || source == planechase.source) return this
        return copy(planechase = planechase.copy(source = source)).bump()
    }

    fun setPlanarSelection(selection: PlanarSelection): StandaloneGame {
        if (state != TableState.LOBBY || !selection.valid() || selection == planechase.selection) return this
        return copy(planechase = planechase.copy(selection = selection)).bump()
    }

    /** Whether phenomena join the digital deck; lobby only. */
    fun setPlanarPhenomena(on: Boolean): StandaloneGame {
        if (state != TableState.LOBBY || on == planechase.phenomena) return this
        return copy(planechase = planechase.copy(phenomena = on)).bump()
    }

    /**
     * The planar controller (CR 901.6): the active player, and in Two-Headed
     * Giant either player of the active team (their team's first seat is the
     * controller, CR 901.12b; TurnHub lets both use the controls).
     */
    private fun planarTurn(index: Int): Boolean = planechase.enabled && state == TableState.RUNNING &&
        active != null && index in mates(active) && players.getOrNull(index)?.eliminated == false

    /**
     * [index] rolls the planar die and got [face] (rolled by the caller). Only the
     * active player(s) roll (CR 901.9, 901.12d); the main phase and an empty stack
     * are for the table to keep. A pending phenomenon is resolved first.
     */
    fun rollPlanarDie(index: Int, face: PlanarFace, fromEffect: Boolean = false): StandaloneGame {
        if (!planarTurn(index) || planechase.phenomenonPending) return this
        return copy(planechase = planechase.roll(index, face, fromEffect)).bump()
    }

    /** The planar controller planeswalks because a card says so. */
    fun planeswalk(index: Int): StandaloneGame =
        if (!planarTurn(index)) this else copy(planechase = planechase.planeswalk()).bump()

    /** The table has resolved the face-up phenomenon: planeswalk away from it (CR 704.6f). */
    fun resolvePhenomenon(index: Int): StandaloneGame =
        if (!planarTurn(index) || !planechase.phenomenonPending) this else copy(planechase = planechase.planeswalk()).bump()

    /** A card says chaos ensues. */
    fun chaosEnsues(index: Int): StandaloneGame =
        if (!planarTurn(index) || planechase.phenomenonPending) this else copy(planechase = planechase.chaosEnsues()).bump()

    /** A card puts the top of the planar deck on the bottom. */
    fun planarTopToBottom(index: Int): StandaloneGame {
        if (!planarTurn(index)) return this
        val next = planechase.topToBottom()
        return if (next === planechase) this else copy(planechase = next).bump()
    }

    /** Deals TurnHub's deck into a digital game that started before the planes were downloaded. */
    fun dealPlanarDeck(deck: List<PlanarCard>): StandaloneGame {
        if (!playing || !planechase.enabled) return this
        val next = planechase.dealtLate(deck)
        return if (next === planechase) this else copy(planechase = next).bump()
    }

    /** Counters on a face-up plane, corrected by any living player, paused or not. */
    fun changePlaneCounter(index: Int, card: String, delta: Int): StandaloneGame {
        if (!playing || !planechase.enabled || players.getOrNull(index)?.eliminated != false) return this
        val next = planechase.changeCounter(card, delta)
        return if (next === planechase) this else copy(planechase = next).bump()
    }

    /**
     * Names (or clears, when blank) the commander [index] is playing, with the printing whose
     * art it shows; with [cardOnly], the card is only their tile's art and is never named as a commander.
     */
    fun setCommander(index: Int, name: String?, art: String? = null, cardOnly: Boolean = false): StandaloneGame {
        if (index !in players.indices) return this
        val clean = name?.trim()?.take(MAX_COMMANDER)?.takeIf { it.isNotEmpty() }
        val printing = art?.takeIf { clean != null && com.turnhub.android.data.TileCardGame.valid(it) }
        // Only a Magic card can be a commander.
        val only = clean != null && (cardOnly || com.turnhub.android.data.TileCardGame.of(printing) != com.turnhub.android.data.TileCardGame.MAGIC)
        if (players[index].commander == clean && players[index].commanderArt == printing && players[index].cardOnly == only) return this
        return update(index) { it.copy(commander = clean, commanderArt = printing, cardOnly = only) }
    }

    private fun finish(winners: List<Int>, nowMs: Long): StandaloneGame = copy(
        state = TableState.GAME_OVER,
        winner = winners.firstOrNull(),
        winners = winners,
        elapsedMs = gameElapsedMs(nowMs),
        runningSinceMs = null,
    ).bump()

    private fun update(index: Int, change: (LocalPlayer) -> LocalPlayer): StandaloneGame =
        copy(players = players.mapIndexed { i, p -> if (i == index) change(p) else p }).bump()

    /** Changes [index] and (Two-Headed Giant) their teammate: what a team shares. */
    private fun updateTeam(index: Int, change: (LocalPlayer) -> LocalPlayer): StandaloneGame {
        val team = mates(index)
        return copy(players = players.mapIndexed { i, p -> if (i in team) change(p) else p }).bump()
    }

    /**
     * The winners once the game is over, or null while it goes on; empty is a
     * draw. Runs after a whole batch of eliminations is applied. Normally one
     * side (a player or a Two-Headed Giant team, given as its first player) is
     * left standing. Star Commander ignores who is left: every living player
     * whose two enemies are out wins together, and nobody alive is a draw.
     */
    private fun decided(): List<Int>? {
        val standing = players.indices.filter { !players[it].eliminated }
        if (star && players.size == StarCommander.PLAYERS) {
            return StarCommander.winners(players.map { it.eliminated }).takeIf { it.isNotEmpty() || standing.isEmpty() }
        }
        return standing.takeIf { s -> s.map { mates(it).first() }.distinct().size <= 1 }?.take(1)
    }

    /**
     * CR 724.4 / 725.4: a leaving holder's monarch or initiative goes to the
     * active player, or the next player in turn order when the active player
     * is leaving too. [leaving] are already marked eliminated.
     */
    private fun passStatuses(leaving: Collection<Int>): StandaloneGame {
        val heir = active?.let { if (!players[it].eliminated) it else nextIn(it) }
        return copy(
            monarch = if (monarch != null && monarch in leaving) heir else monarch,
            initiative = if (initiative != null && initiative in leaving) heir else initiative,
        )
    }

    /** The next player still in, clockwise after [index]. */
    private fun nextIn(index: Int): Int? = (1..players.size)
        .map { (index + it) % players.size }
        .firstOrNull { !players[it].eliminated && it != index }

    /** The finished match as Atlas will import it; null until it ends. */
    fun record(): GameRecord? {
        if (state != TableState.GAME_OVER || gameId.isEmpty()) return null
        return GameRecord(
            recordId = gameId,
            profile = profile,
            startingLife = startingLife,
            startedAtMs = startedAtMs,
            durationMs = elapsedMs,
            starter = starter,
            winner = winner,
            winners = winners,
            twoHeadedGiant = twoHeadedGiant,
            players = players.map { p ->
                GameRecord.Player(
                    name = p.name,
                    localId = p.localId,
                    profileId = p.profileId,
                    finalLife = p.life,
                    turnsCompleted = p.turnsCompleted,
                    turnMs = p.turnMs,
                    fastestTurnMs = p.fastestTurnMs,
                    longestTurnMs = p.longestTurnMs,
                    outOrder = p.outOrder,
                    conceded = p.conceded,
                    commanderDamageReceived = p.commanderDamage.values.sumOf { it.sum() },
                    counters = p.counters,
                    commanderBySource = p.commanderDamage,
                    demerits = p.demerits,
                    extensionsUsed = p.extensionsUsed,
                )
            },
        )
    }

    // --- drawing it with the tablet screen ---------------------------------------------

    /**
     * The game as a [TableSummary], so the tablet table draws it exactly as it
     * draws an Atlas game. [monoNowMs] is [com.turnhub.android.domain.TableClock]'s
     * clock, which the screen extrapolates from while the game runs.
     */
    fun toSummary(nowMs: Long, monoNowMs: Long, personalizations: Map<String, LocalPersonalization> = emptyMap()): TableSummary {
        val elapsed = gameElapsedMs(nowMs)
        return TableSummary(
            atlasId = "",
            bootId = gameId,
            firmwareVersion = "",
            revision = revision,
            state = state,
            settings = TableSettings(profile, startingLife, autoEliminateAtZeroLife = autoEliminateAtZeroLife,
                autoEliminateAtPoisonLimit = autoEliminateAtPoisonLimit, autoEliminateAtCommanderDamage = autoEliminateAtCommanderDamage, poisonLimit = poisonLimit,
                turnTimerMs = turnTimerMs, twoHeadedGiant = twoHeadedGiant, houseRules = houseRules),
            host = null,
            starterPlayerNumber = starter?.plus(1),
            activePlayerNumber = active?.plus(1),
            winnerPlayerNumber = winner?.plus(1),
            winnerPlayerNumbers = winners.map { it + 1 },
            monarchPlayerNumber = monarch?.plus(1),
            initiativePlayerNumber = initiative?.plus(1),
            pending = PendingDecisions(null, 0, null, null, null),
            gameElapsedMs = elapsed,
            turnElapsedMs = (elapsed - turnStartedAtElapsedMs).coerceAtLeast(0),
            turnTimer = turnTimer(nowMs),
            localTimerRules = timerRules,
            turnExtensionMs = turnExtensionMs,
            receivedAtMs = monoNowMs,
            players = players.mapIndexed { i, p ->
                TablePlayer(
                    playerNumber = i + 1,
                    label = p.name,
                    hasName = true,
                    controller = seatHandle(i),
                    slot = 1,
                    participantId = (i + 1).toLong(),
                    eliminated = p.eliminated,
                    conceded = p.conceded,
                    counters = p.counters,
                    demerits = p.demerits,
                    extensionsAvailable = p.extensionsEarned - p.extensionsUsed,
                    // Any game can show any game's card on a tile; only Commander formats call
                    // it a commander, and never a card picked only for its art.
                    commander = p.commander,
                    commanderArt = p.commanderArt,
                    cardOnly = p.cardOnly || !profile.commanderRules,
                    turn = p.turn,
                    life = if (state == TableState.LOBBY) null else p.life,
                    turnsCompleted = p.turnsCompleted.toLong(),
                    commanderDamage = p.commanderDamage.map { (source, damage) -> CommanderDamage(source + 1, damage) },
                    lifeRequest = null,
                    team = if (twoHeadedGiant) i / 2 + 1 else null,
                    avatar = personalizations[p.localId]?.icon(),
                    color = personalizations[p.localId]?.color,
                    tileBackground = personalizations[p.localId]?.tileBackground ?: com.turnhub.android.domain.TileBackground(),
                )
            },
            physicalSigils = emptyList(),
            pokemonCheckup = pokemonCheckup,
        )
    }

    companion object {
        const val MAX_PLAYERS = 8
        const val MAX_NAME = 32
        const val MAX_COMMANDER = 80
        /** A Scryfall card ID (a UUID). */
        val PRINTING_ID = Regex("[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}")
        /** Every second seat of the star: today's allies sit across from each other. */
        val SEAN_MODE_ORDER = listOf(0, 2, 4, 1, 3)
        const val MAX_LIFE = 99_999
        val TURN_TIMER_RANGE_MS = 15_000L..3_600_000L
        val TURN_TIMER_PRESETS_MS = listOf(0L, 60_000L, 120_000L, 180_000L, 300_000L, 600_000L)
        private const val WARNING_MS = 10_000L
        private const val LONG_TURN_MS = 300_000L

        /** A Two-Headed Giant team's total is 30 for Magic and 60 for Commander. */
        fun defaultLife(profile: GameProfile, twoHeadedGiant: Boolean = false): Int = when (profile) {
            GameProfile.MTG_COMMANDER -> if (twoHeadedGiant) 60 else 40
            GameProfile.MTG_STAR_COMMANDER -> 40
            GameProfile.MTG -> if (twoHeadedGiant) 30 else 20
            GameProfile.YUGIOH -> 8000
            GameProfile.POKEMON -> 6
            GameProfile.LORCANA -> 20
            else -> 20
        }

        /** How long after the start Back to setup stays available. */
        const val UNDO_START_MS = 3 * 60_000L
        /** A finished game shorter than this isn't counted unless the table saves it (a test or a wrong setup). */
        const val SHORT_GAME_MS = 60_000L

        /** Counter kinds for once-per-turn actions and Pokémon's Special Conditions ([TurnAction], [SpecialCondition] names follow). */
        const val ACTION_KIND = "turn-action:"
        const val CONDITION_KIND = "pokemon-condition:"

        private const val SOLO_POISON = 10
        private const val TEAM_POISON = 15

        /** Each player is drawn as a virtual controller handle (8 up), slot 1. */
        fun seatHandle(index: Int) = ControllerHandle(ControllerHandle.VIRTUAL.first + index)

        fun indexOf(handle: Int): Int = handle - ControllerHandle.VIRTUAL.first
    }
}
