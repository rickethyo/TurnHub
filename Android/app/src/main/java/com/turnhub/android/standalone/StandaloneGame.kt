package com.turnhub.android.standalone

import com.turnhub.android.domain.ControllerHandle
import com.turnhub.android.domain.TablePlayer
import com.turnhub.android.domain.TableSummary
import com.turnhub.android.protocol.CommanderDamage
import com.turnhub.android.protocol.GameProfile
import com.turnhub.android.protocol.PendingDecisions
import com.turnhub.android.protocol.TableSettings
import com.turnhub.android.protocol.TableState
import com.turnhub.android.protocol.TurnTimer
import com.turnhub.android.protocol.TurnTimerPhase
import kotlin.random.Random

/**
 * One player of a standalone game. [profileId] is an Atlas profile picked from
 * the cached list, so the finished record can credit it on the next connect;
 * null means Atlas matches the name instead.
 */
data class LocalPlayer(
    val name: String,
    val profileId: String? = null,
    val life: Int = 0,
    val eliminated: Boolean = false,
    /** 1 for the first player out, 2 for the next, and so on. */
    val outOrder: Int? = null,
    val turnsCompleted: Int = 0,
    /** Time spent on completed turns, and the quickest and longest of them (0 until one completes). */
    val turnMs: Long = 0,
    val fastestTurnMs: Long = 0,
    val longestTurnMs: Long = 0,
    /** Commander damage received, by source player index: one total per commander (1 and 2). */
    val commanderDamage: Map<Int, List<Int>> = emptyMap(),
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
    val players: List<LocalPlayer> = emptyList(),
    /** Index into [players] of whose turn it is. */
    val active: Int? = null,
    val starter: Int? = null,
    val winner: Int? = null,
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

    // --- the lobby ------------------------------------------------------------------

    fun addPlayer(name: String, profileId: String? = null): StandaloneGame {
        val trimmed = name.trim().take(MAX_NAME)
        if (state != TableState.LOBBY || trimmed.isEmpty() || players.size >= MAX_PLAYERS) return this
        if (profileId != null && players.any { it.profileId == profileId }) return this
        return copy(players = players + LocalPlayer(trimmed, profileId)).bump()
    }

    fun removePlayer(index: Int): StandaloneGame {
        if (state != TableState.LOBBY || index !in players.indices) return this
        return copy(players = players.filterIndexed { i, _ -> i != index }).bump()
    }

    /** Moves a player one place earlier ([by] = -1) or later (+1) in turn order. */
    fun movePlayer(index: Int, by: Int): StandaloneGame {
        val to = index + by
        if (state != TableState.LOBBY || index !in players.indices || to !in players.indices) return this
        val list = players.toMutableList()
        list[index] = players[to]
        list[to] = players[index]
        return copy(players = list).bump()
    }

    fun setFormat(profile: GameProfile, startingLife: Int = defaultLife(profile)): StandaloneGame {
        if (state != TableState.LOBBY) return this
        return copy(profile = profile, startingLife = startingLife.coerceIn(1, MAX_LIFE)).bump()
    }

    fun setStartingLife(life: Int): StandaloneGame {
        if (state != TableState.LOBBY || life !in 1..MAX_LIFE) return this
        return copy(startingLife = life).bump()
    }

    /** Starts a match with a random first player; needs two players. */
    fun start(nowMs: Long, gameId: String, random: Random = Random.Default): StandaloneGame {
        if (state != TableState.LOBBY && state != TableState.GAME_OVER) return this
        if (players.size < 2) return this
        val first = random.nextInt(players.size)
        return copy(
            gameId = gameId,
            state = TableState.RUNNING,
            players = players.map { LocalPlayer(it.name, it.profileId, life = startingLife) },
            active = first,
            starter = first,
            winner = null,
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
            players = players.map { LocalPlayer(it.name, it.profileId) },
            revision = revision + 1,
        )
    }

    // --- the table ------------------------------------------------------------------

    fun pass(index: Int, nowMs: Long): StandaloneGame {
        if (state != TableState.RUNNING || index != active) return this
        val next = nextIn(index) ?: return this
        val now = gameElapsedMs(nowMs)
        val took = (now - turnStartedAtElapsedMs).coerceAtLeast(0)
        return copy(
            players = players.mapIndexed { i, p ->
                if (i != index) p else p.copy(
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

    fun changeLife(index: Int, delta: Int): StandaloneGame {
        val player = players.getOrNull(index) ?: return this
        if (!playing || player.eliminated || delta == 0) return this
        val life = (player.life.toLong() + delta).coerceIn(-MAX_LIFE.toLong(), MAX_LIFE.toLong()).toInt()
        return update(index) { it.copy(life = life) }
    }

    /**
     * Commander damage [index] received from [source]'s [commander] (1 or 2).
     * It costs the same life, as on Atlas; a total never goes below zero.
     */
    fun commanderDamage(index: Int, source: Int, commander: Int, delta: Int): StandaloneGame {
        val player = players.getOrNull(index) ?: return this
        if (!playing || player.eliminated || source == index || source !in players.indices || commander !in 1..2) return this
        val totals = player.commanderDamage[source] ?: listOf(0, 0)
        val now = totals[commander - 1]
        val next = (now + delta).coerceIn(0, MAX_LIFE)
        val applied = next - now
        if (applied == 0) return this
        val updated = totals.toMutableList().also { it[commander - 1] = next }
        return update(index) { it.copy(commanderDamage = it.commanderDamage + (source to updated), life = it.life - applied) }
    }

    /** The player leaves the game; when one is left standing, they win. */
    fun concede(index: Int, nowMs: Long): StandaloneGame {
        val player = players.getOrNull(index) ?: return this
        if (!playing || player.eliminated) return this
        val out = players.count { it.eliminated } + 1
        var game = update(index) { it.copy(eliminated = true, outOrder = out) }
        val standing = game.players.indices.filter { !game.players[it].eliminated }
        if (standing.size <= 1) return game.finish(standing.firstOrNull(), nowMs)
        if (active == index) {
            val next = game.nextIn(index) ?: return game
            game = game.copy(active = next, turnStartedAtElapsedMs = game.gameElapsedMs(nowMs))
        }
        return game
    }

    /** The table agreed this player won (the claim is armed on the panel, so one tap can't end a game). */
    fun claimWin(index: Int, nowMs: Long): StandaloneGame {
        val player = players.getOrNull(index) ?: return this
        if (!playing || player.eliminated) return this
        return finish(index, nowMs)
    }

    /** The table ends the game with no winner (it still counts as played). */
    fun endWithoutWinner(nowMs: Long): StandaloneGame = if (!playing) this else finish(null, nowMs)

    /** The same players play again; a new [gameId] makes it a separate record. */
    fun rematch(nowMs: Long, gameId: String, random: Random = Random.Default): StandaloneGame {
        if (state != TableState.GAME_OVER) return this
        return start(nowMs, gameId, random)
    }

    private fun finish(winner: Int?, nowMs: Long): StandaloneGame = copy(
        state = TableState.GAME_OVER,
        winner = winner,
        elapsedMs = gameElapsedMs(nowMs),
        runningSinceMs = null,
    ).bump()

    private fun update(index: Int, change: (LocalPlayer) -> LocalPlayer): StandaloneGame =
        copy(players = players.mapIndexed { i, p -> if (i == index) change(p) else p }).bump()

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
            players = players.map { p ->
                GameRecord.Player(
                    name = p.name,
                    profileId = p.profileId,
                    finalLife = p.life,
                    turnsCompleted = p.turnsCompleted,
                    turnMs = p.turnMs,
                    fastestTurnMs = p.fastestTurnMs,
                    longestTurnMs = p.longestTurnMs,
                    outOrder = p.outOrder,
                    commanderDamageReceived = p.commanderDamage.values.sumOf { it.sum() },
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
    fun toSummary(nowMs: Long, monoNowMs: Long): TableSummary {
        val elapsed = gameElapsedMs(nowMs)
        return TableSummary(
            atlasId = "",
            bootId = gameId,
            firmwareVersion = "",
            revision = revision,
            state = state,
            settings = TableSettings(profile, startingLife),
            host = null,
            starterPlayerNumber = starter?.plus(1),
            activePlayerNumber = active?.plus(1),
            winnerPlayerNumber = winner?.plus(1),
            pending = PendingDecisions(null, 0, null, null, null),
            gameElapsedMs = elapsed,
            turnElapsedMs = (elapsed - turnStartedAtElapsedMs).coerceAtLeast(0),
            turnTimer = TurnTimer(TurnTimerPhase.NORMAL, null),
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
                    life = if (state == TableState.LOBBY) null else p.life,
                    turnsCompleted = p.turnsCompleted.toLong(),
                    commanderDamage = p.commanderDamage.map { (source, damage) -> CommanderDamage(source + 1, damage) },
                    lifeRequest = null,
                )
            },
            physicalSigils = emptyList(),
        )
    }

    companion object {
        const val MAX_PLAYERS = 8
        const val MAX_NAME = 32
        const val MAX_LIFE = 99_999

        fun defaultLife(profile: GameProfile): Int = when (profile) {
            GameProfile.MTG_COMMANDER -> 40
            GameProfile.YUGIOH -> 8000
            else -> 20
        }

        /** Each player is drawn as a virtual controller handle (8 up), slot 1. */
        fun seatHandle(index: Int) = ControllerHandle(ControllerHandle.VIRTUAL.first + index)

        fun indexOf(handle: Int): Int = handle - ControllerHandle.VIRTUAL.first
    }
}
