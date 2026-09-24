package com.turnhub.android.domain

import com.turnhub.android.protocol.TableState

/**
 * Renders Atlas's sampled clocks between polls. Atlas stays authoritative:
 * these only extrapolate the latest snapshot by the local time since it
 * arrived, and every new snapshot replaces the estimate. Nothing advances
 * unless Atlas said the game is RUNNING.
 */
object TableClock {

    /** Local monotonic milliseconds; the same clock the repository stamps with. */
    fun nowMs(): Long = System.nanoTime() / 1_000_000

    private fun sinceReceived(summary: TableSummary, nowMs: Long): Long =
        (nowMs - summary.receivedAtMs).coerceAtLeast(0)

    fun turnElapsedMs(summary: TableSummary, nowMs: Long): Long =
        if (summary.state == TableState.RUNNING) summary.turnElapsedMs + sinceReceived(summary, nowMs)
        else summary.turnElapsedMs

    fun gameElapsedMs(summary: TableSummary, nowMs: Long): Long =
        if (summary.state == TableState.RUNNING) summary.gameElapsedMs + sinceReceived(summary, nowMs)
        else summary.gameElapsedMs

    /**
     * Turn-timer countdown left, rendered forward between polls and never below
     * zero. Null when the timer is off or no turn is running.
     */
    fun turnRemainingMs(summary: TableSummary, nowMs: Long): Long? {
        val sampled = summary.turnTimer?.remainingMs ?: return null
        return if (summary.state == TableState.RUNNING) (sampled - sinceReceived(summary, nowMs)).coerceAtLeast(0)
        else sampled
    }

    /** How far the current turn has run past its timer; 0 when on time or untimed. */
    fun overtimeMs(summary: TableSummary, nowMs: Long): Long =
        if (!summary.settings.turnTimerEnabled) 0
        else (turnElapsedMs(summary, nowMs) - summary.settings.turnTimerMs).coerceAtLeast(0)

    /** Remaining cancellable-pass grace; counts down, never below zero. */
    fun passGraceRemainingMs(summary: TableSummary, nowMs: Long): Long =
        if (summary.pending.passPlayer == null) 0
        else (summary.pending.passGraceRemainingMs - sinceReceived(summary, nowMs)).coerceAtLeast(0)

    /** `m:ss`, or `h:mm:ss` from an hour up. */
    fun format(ms: Long): String {
        val totalSeconds = ms.coerceAtLeast(0) / 1000
        val hours = totalSeconds / 3600
        val minutes = (totalSeconds % 3600) / 60
        val seconds = totalSeconds % 60
        return if (hours > 0) "%d:%02d:%02d".format(hours, minutes, seconds)
        else "%d:%02d".format(minutes, seconds)
    }
}
