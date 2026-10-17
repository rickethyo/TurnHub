package com.turnhub.android.domain

import com.turnhub.android.protocol.TableState

/** Identifies the turn a recipient chooser was opened for; unrelated edits do not invalidate it. */
data class CooperativeTurn(val gameId: String, val playerNumber: Int, val round: Int, val turnsCompleted: Long)

fun TableSummary.cooperativeTurn(): CooperativeTurn? {
    if (!settings.profile.cooperative || state != TableState.RUNNING) return null
    val player = players.firstOrNull { it.playerNumber == activePlayerNumber && !it.eliminated } ?: return null
    return CooperativeTurn(bootId, player.playerNumber, round ?: return null, player.turnsCompleted)
}
