package com.turnhub.android.ui.tablet

import com.turnhub.android.domain.TableSummary
import com.turnhub.android.protocol.TableState

/** Facts from the current host snapshot; team turns are counted once. */
internal fun publicPanelFacts(unit: TableUnit, summary: TableSummary): List<String> = buildList {
    add("Turns completed ${unit.members.maxOfOrNull { it.turnsCompleted } ?: 0}")
    val profile = summary.settings.profile
    when {
        profile.commanderRules -> add("Commander casts ${unit.members.sumOf { it.counters.commanderCasts.sum() }}")
        profile.pokemon -> unit.members.firstOrNull()?.life?.let { add("Prizes left $it") }
        profile.lorcana -> unit.members.firstOrNull()?.life?.let { add("Lore $it / ${summary.settings.startingLife}") }
    }
}

internal fun publicTurnLabel(unit: TableUnit, summary: TableSummary): String? = when {
    unit.out || unit.members.none { summary.hasTurn(it.playerNumber) } -> null
    summary.state == TableState.RUNNING -> "Active turn"
    summary.state == TableState.PAUSED -> "Turn paused"
    else -> null
}
