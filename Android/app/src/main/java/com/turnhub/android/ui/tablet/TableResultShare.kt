package com.turnhub.android.ui.tablet

import com.turnhub.android.domain.TableClock
import com.turnhub.android.domain.TableSummary
import com.turnhub.android.protocol.TableState
import com.turnhub.android.ui.components.displayName

/** A share caption from the host's finished snapshot; no invented match record or statistics. */
internal fun TableSummary.resultShareSummary(): String {
    require(state == TableState.GAME_OVER)
    val winners = players.filter { isWinner(it.playerNumber) }.sortedBy { it.playerNumber }
    return buildString {
        appendLine(when {
            settings.profile.cooperative -> if (winners.isEmpty()) "The investigators lost." else "The investigators win!"
            winners.isEmpty() -> "Game over: a draw!"
            winners.size == 1 -> "🏆 ${winners.single().label} won!"
            else -> "🏆 ${winners.joinToString(" and ") { it.label }} shared the win!"
        })
        appendLine("${settings.profile.displayName()}${if (settings.twoHeadedGiant) " · Two-Headed Giant" else ""} · ${players.size} players · ${TableClock.format(gameElapsedMs)}")
        players.sortedBy { it.playerNumber }.forEach { player ->
            append(player.label)
            player.life?.let { append(": $it life") }
            append(" · ${player.turnsCompleted} turns")
            if (isWinner(player.playerNumber)) append(" · winner")
            if (player.conceded) append(" · conceded")
            appendLine()
        }
        append("Played with TurnHub")
    }
}
