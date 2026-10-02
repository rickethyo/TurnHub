package com.turnhub.android.domain

import com.turnhub.android.protocol.TableState

/**
 * What the lock-screen turn notification shows (V1 phase 8), derived from
 * Atlas's latest snapshot. Times are wall-clock milliseconds so the
 * notification's own chronometer can run between polls. Display only: Atlas
 * owns every turn and timer.
 */
data class LiveTurn(
    val state: TableState,
    /** Whose turn it is, or null when no turn runs. */
    val activeName: String?,
    /** The turn belongs to this phone's player. */
    val mine: Boolean,
    /** 1-based turn number of the active player, when known. */
    val turnNumber: Long?,
    /** When the current turn started (counting up), or null. */
    val turnStartedAtMs: Long?,
    /** When the turn timer reaches zero (counting down), or null when untimed or paused. */
    val timerEndsAtMs: Long?,
) {
    /** Changes that deserve a new notification; clock drift between polls does not. */
    fun sameMoment(other: LiveTurn?): Boolean {
        if (other == null) return false
        return state == other.state && activeName == other.activeName && mine == other.mine &&
            turnNumber == other.turnNumber && near(turnStartedAtMs, other.turnStartedAtMs) &&
            near(timerEndsAtMs, other.timerEndsAtMs)
    }

    private fun near(a: Long?, b: Long?) = if (a == null || b == null) a == b else kotlin.math.abs(a - b) < DRIFT_MS

    companion object {
        private const val DRIFT_MS = 2_000L

        /**
         * Null unless this phone's player is seated in a game that is starting,
         * running or paused: the only times a turn clock means anything.
         */
        fun from(summary: TableSummary, myNumber: Int?, nowMs: Long, wallNowMs: Long): LiveTurn? {
            if (myNumber == null || summary.players.none { it.playerNumber == myNumber }) return null
            if (summary.state != TableState.STARTING && summary.state != TableState.RUNNING &&
                summary.state != TableState.PAUSED
            ) return null
            val active = summary.players.firstOrNull { it.playerNumber == summary.activePlayerNumber }
            val running = summary.state == TableState.RUNNING
            val remaining = TableClock.turnRemainingMs(summary, nowMs)
            return LiveTurn(
                state = summary.state,
                activeName = active?.label,
                mine = active != null && active.playerNumber == myNumber,
                turnNumber = active?.let { it.turnsCompleted + 1 },
                turnStartedAtMs = if (running && active != null) wallNowMs - TableClock.turnElapsedMs(summary, nowMs) else null,
                timerEndsAtMs = if (running && remaining != null && remaining > 0) wallNowMs + remaining else null,
            )
        }
    }
}
