package com.turnhub.android.domain

import com.turnhub.android.protocol.TableState
import com.turnhub.android.protocol.TurnTimerPhase

/**
 * How the Home screen words the turn clock. The phase always comes from Atlas
 * ([TableSummary.turnTimer]); this only chooses text, so meaning never rests on
 * color alone (Documentation/engineering/ACCESSIBILITY.md).
 */
data class TurnTimerStatus(
    /** Caption above the clock, e.g. "Time left". */
    val label: String,
    /** The clock itself, e.g. "0:42" or "+1:05". */
    val value: String,
    val phase: TurnTimerPhase,
    /** A sentence for a banner and screen readers, or null when nothing needs saying. */
    val notice: String?,
) {
    companion object {
        /** Null when no turn is shown (lobby, starting, no active player). */
        fun of(summary: TableSummary, nowMs: Long): TurnTimerStatus? {
            if (summary.activePlayerNumber == null ||
                (summary.state != TableState.RUNNING && summary.state != TableState.PAUSED)
            ) {
                return null
            }
            val phase = summary.turnTimer?.phase ?: TurnTimerPhase.NORMAL
            val remaining = TableClock.turnRemainingMs(summary, nowMs)
            return when {
                phase == TurnTimerPhase.EXPIRED -> TurnTimerStatus(
                    label = "Over time",
                    value = "+" + TableClock.format(TableClock.overtimeMs(summary, nowMs)),
                    phase = phase,
                    notice = "Time's up. The turn continues until the player passes.",
                )
                remaining != null -> TurnTimerStatus(
                    label = "Time left",
                    value = TableClock.format(remaining),
                    phase = phase,
                    notice = if (phase == TurnTimerPhase.WARNING) "Ten seconds or less left in this turn." else null,
                )
                else -> TurnTimerStatus(
                    label = "Turn",
                    value = TableClock.format(TableClock.turnElapsedMs(summary, nowMs)),
                    phase = phase,
                    notice = if (phase == TurnTimerPhase.LONG_TURN) "Long turn: over five minutes." else null,
                )
            }
        }

        /** A setting's name, e.g. "Off", "2 minutes", "90 seconds". */
        fun settingLabel(turnTimerMs: Long): String = when {
            turnTimerMs <= 0 -> "Off"
            turnTimerMs % 60_000 == 0L -> (turnTimerMs / 60_000).let { if (it == 1L) "1 minute" else "$it minutes" }
            else -> "${turnTimerMs / 1000} seconds"
        }
    }
}
