package com.turnhub.android.standalone

import com.turnhub.android.domain.ControllerHandle
import com.turnhub.android.domain.StarCommander
import com.turnhub.android.domain.TablePlayer
import com.turnhub.android.domain.TableSummary
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
    /** 0 = off; else the countdown each turn gets. As on Atlas, running out never passes the turn. */
    val turnTimerMs: Long = 0,
    /**
     * Two-Headed Giant (Magic and Commander), as on Atlas (GAMEPLAY.md): neighbours
     * in turn order (players 1+2, 3+4, ...) are teams sharing one life total,
     * poison count and turn, and leaving together.
     */
    val twoHeadedGiant: Boolean = false,
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
    val revision: Long = 0,
) {
    fun gameElapsedMs(nowMs: Long): Long = elapsedMs + (runningSinceMs?.let { (nowMs - it).coerceAtLeast(0) } ?: 0)

    private fun bump(): StandaloneGame = copy(revision = revision + 1)

    private val playing: Boolean get() = state == TableState.RUNNING || state == TableState.PAUSED

    private val magic: Boolean get() = profile.magicRules

    private val star: Boolean get() = profile.starCommander

    /** [index] and, in Two-Headed Giant, their teammate. */
    fun mates(index: Int): List<Int> =
        if (!twoHeadedGiant) listOf(index) else (index - index % 2).let { listOf(it, it + 1) }.filter { it in players.indices }

    /** Two players, (Two-Headed Giant) an even table of at least four, or (Star Commander) exactly five. */
    val canStart: Boolean get() = if (star) players.size == StarCommander.PLAYERS
        else players.size >= 2 && (!twoHeadedGiant || (players.size >= 4 && players.size % 2 == 0))

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
        return copy(players = players.filterIndexed { i, _ -> i != index }).bump()
    }

    /** Moves a player one place earlier ([by] = -1) or later (+1) in turn order. */
    fun movePlayer(index: Int, by: Int): StandaloneGame = swapPlayers(index, index + by)

    /** Trades two players' seats (and so their places in turn order); lobby only. */
    fun swapPlayers(a: Int, b: Int): StandaloneGame {
        if (state != TableState.LOBBY || a !in players.indices || b !in players.indices || a == b) return this
        val list = players.toMutableList()
        list[a] = players[b]
        list[b] = players[a]
        return copy(players = list).bump()
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
        ).bump()
    }

    /** The game mode: Two-Headed Giant (Magic and Commander, not Star Commander) or standard; lobby only. */
    fun setTwoHeadedGiant(on: Boolean): StandaloneGame {
        if (state != TableState.LOBBY || on == twoHeadedGiant || (on && (!magic || star))) return this
        return copy(
            twoHeadedGiant = on,
            startingLife = defaultLife(profile, on),
            poisonLimit = if (on) TEAM_POISON else SOLO_POISON,
        ).bump()
    }

    fun setStartingLife(life: Int): StandaloneGame {
        if (state != TableState.LOBBY || life !in 1..MAX_LIFE) return this
        return copy(startingLife = life).bump()
    }

    /** Off (0) or 15 s to 60 min in whole seconds, Atlas's range; set in the lobby only. */
    fun setTurnTimer(ms: Long): StandaloneGame {
        if (state != TableState.LOBBY || (ms != 0L && (ms !in TURN_TIMER_RANGE_MS || ms % 1000 != 0L))) return this
        return copy(turnTimerMs = ms).bump()
    }

    /** The turn clock's phase, derived like Atlas's (GAMEPLAY.md "Turn timer"). */
    fun turnTimer(nowMs: Long): TurnTimer {
        if (active == null || !playing) return TurnTimer(TurnTimerPhase.NORMAL, null)
        val turn = (gameElapsedMs(nowMs) - turnStartedAtElapsedMs).coerceAtLeast(0)
        if (turnTimerMs <= 0) {
            return TurnTimer(if (turn >= LONG_TURN_MS) TurnTimerPhase.LONG_TURN else TurnTimerPhase.NORMAL, null)
        }
        val remaining = (turnTimerMs - turn).coerceAtLeast(0)
        val phase = when {
            remaining == 0L -> TurnTimerPhase.EXPIRED
            remaining <= WARNING_MS -> TurnTimerPhase.WARNING
            else -> TurnTimerPhase.NORMAL
        }
        return TurnTimer(phase, remaining)
    }

    /** Starts a match with a random first player; needs two players. */
    fun start(nowMs: Long, gameId: String, random: Random = Random.Default): StandaloneGame {
        if (state != TableState.LOBBY && state != TableState.GAME_OVER) return this
        if (!canStart) return this
        // A team game starts with a team: its first player holds the turn for both.
        val first = if (twoHeadedGiant) random.nextInt(players.size / 2) * 2 else random.nextInt(players.size)
        return copy(
            gameId = gameId,
            state = TableState.RUNNING,
            players = players.map { LocalPlayer(it.name, it.profileId, life = startingLife, localId = it.localId) },
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
            twoHeadedGiant = twoHeadedGiant,
            players = players.map { LocalPlayer(it.name, it.profileId, localId = it.localId) },
            revision = revision + 1,
        )
    }

    // --- the table ------------------------------------------------------------------

    fun pass(index: Int, nowMs: Long): StandaloneGame {
        val holder = active ?: return this
        // Either teammate may pass the team's turn; it goes to the next team.
        val team = mates(holder)
        if (state != TableState.RUNNING || index !in team) return this
        val next = nextIn(team.last()) ?: return this
        val now = gameElapsedMs(nowMs)
        val took = (now - turnStartedAtElapsedMs).coerceAtLeast(0)
        return copy(
            players = players.mapIndexed { i, p ->
                if (i !in team || p.eliminated) p else p.copy(
                    turnsCompleted = p.turnsCompleted + 1,
                    turnMs = p.turnMs + took,
                    fastestTurnMs = if (p.fastestTurnMs == 0L) took else minOf(p.fastestTurnMs, took),
                    longestTurnMs = maxOf(p.longestTurnMs, took),
                )
            },
            active = next,
            turnStartedAtElapsedMs = now,
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
            "poisonLimit" -> if (value in 1..MAX_LIFE) copy(poisonLimit = value) else return this
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
            game = game.copy(active = next, turnStartedAtElapsedMs = game.gameElapsedMs(nowMs))
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
            !p.eliminated && ((autoEliminateAtZeroLife && p.life <= 0) ||
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
            game = game.copy(active = game.nextIn(mates(active!!).last()), turnStartedAtElapsedMs = game.gameElapsedMs(nowMs))
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
    fun rematch(nowMs: Long, gameId: String, random: Random = Random.Default): StandaloneGame {
        if (state != TableState.GAME_OVER) return this
        return start(nowMs, gameId, random)
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
                turnTimerMs = turnTimerMs, twoHeadedGiant = twoHeadedGiant),
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
                    life = if (state == TableState.LOBBY) null else p.life,
                    turnsCompleted = p.turnsCompleted.toLong(),
                    commanderDamage = p.commanderDamage.map { (source, damage) -> CommanderDamage(source + 1, damage) },
                    lifeRequest = null,
                    team = if (twoHeadedGiant) i / 2 + 1 else null,
                    avatar = personalizations[p.localId]?.icon(),
                    color = personalizations[p.localId]?.color,
                )
            },
            physicalSigils = emptyList(),
        )
    }

    companion object {
        const val MAX_PLAYERS = 8
        const val MAX_NAME = 32
        const val MAX_LIFE = 99_999
        val TURN_TIMER_RANGE_MS = 15_000L..3_600_000L
        val TURN_TIMER_PRESETS_MS = listOf(0L, 60_000L, 120_000L, 180_000L, 300_000L)
        private const val WARNING_MS = 10_000L
        private const val LONG_TURN_MS = 300_000L

        /** A Two-Headed Giant team's total is 30 for Magic and 60 for Commander. */
        fun defaultLife(profile: GameProfile, twoHeadedGiant: Boolean = false): Int = when (profile) {
            GameProfile.MTG_COMMANDER -> if (twoHeadedGiant) 60 else 40
            GameProfile.MTG_STAR_COMMANDER -> 40
            GameProfile.MTG -> if (twoHeadedGiant) 30 else 20
            GameProfile.YUGIOH -> 8000
            else -> 20
        }

        private const val SOLO_POISON = 10
        private const val TEAM_POISON = 15

        /** Each player is drawn as a virtual controller handle (8 up), slot 1. */
        fun seatHandle(index: Int) = ControllerHandle(ControllerHandle.VIRTUAL.first + index)

        fun indexOf(handle: Int): Int = handle - ControllerHandle.VIRTUAL.first
    }
}
