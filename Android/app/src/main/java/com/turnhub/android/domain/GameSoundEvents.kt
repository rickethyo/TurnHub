package com.turnhub.android.domain

import com.turnhub.android.protocol.TableState

enum class GameSoundCue { START, PASS, PAUSE, RESUME, WIN, DRAW }

/** Phone presentation only; independent of profile/Sigil audio preferences. */
data class GameSoundPreferences(val enabled: Boolean = true, val volume: Int = 50) {
    init { require(volume in 0..100) }
}

data class GameSoundSource(val atlasId: String, val bootId: String, val game: Int)

/** Small projection: clocks, names and artwork never count as sound events. */
data class GameSoundSnapshot(
    val source: GameSoundSource,
    val revision: Long,
    val state: TableState,
    val active: Int?,
    val turns: Map<Long, Long>,
    val draw: Boolean,
) {
    companion object {
        fun from(summary: TableSummary) = GameSoundSnapshot(
            // Standalone's bootId is its match ID, which changes at start/rematch.
            GameSoundSource(summary.atlasId, if (summary.atlasId.isBlank()) "" else summary.bootId, summary.game),
            summary.revision, summary.state, summary.activePlayerNumber,
            summary.players.associate { it.participantId to it.turnsCompleted }, summary.endedInDraw,
        )
    }
}

/** Observes confirmed state transitions, never button presses or optimistic edits. */
class GameSoundEvents {
    private var previous: GameSoundSnapshot? = null
    private var previouslyEnabled = false

    fun reset() { previous = null; previouslyEnabled = false }

    fun observe(current: GameSoundSnapshot?, enabled: Boolean): GameSoundCue? {
        val before = previous
        val wasEnabled = previouslyEnabled
        previous = current
        previouslyEnabled = enabled
        // Reconnect/resume/unmute takes a fresh baseline; missed events are never replayed.
        if (!enabled || !wasEnabled || before == null || current == null ||
            before.source != current.source || current.revision < before.revision) return null
        return when {
            current.state == TableState.GAME_OVER && before.state != TableState.GAME_OVER ->
                if (current.draw) GameSoundCue.DRAW else GameSoundCue.WIN
            current.state == TableState.RUNNING && before.state in listOf(TableState.LOBBY, TableState.STARTING, TableState.GAME_OVER) ->
                GameSoundCue.START
            current.state == TableState.PAUSED && before.state == TableState.RUNNING -> GameSoundCue.PAUSE
            current.state == TableState.RUNNING && before.state == TableState.PAUSED -> GameSoundCue.RESUME
            current.state == TableState.RUNNING && before.state == TableState.RUNNING &&
                (current.active != before.active || current.turns.any { (id, turns) ->
                    before.turns[id]?.let { turns > it } == true
                }) -> GameSoundCue.PASS
            else -> null
        }
    }
}
