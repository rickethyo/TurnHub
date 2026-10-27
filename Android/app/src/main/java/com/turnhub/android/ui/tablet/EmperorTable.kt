package com.turnhub.android.ui.tablet

import androidx.compose.foundation.layout.padding
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import com.turnhub.android.domain.Emperor
import com.turnhub.android.domain.TableSummary
import com.turnhub.android.ui.theme.TurnHubPalette
import com.turnhub.android.ui.theme.palette

/**
 * The Emperor table (owner, 2026-10-21): team A along the far side and team B
 * along the near side, each emperor in the middle of its side, so the generals
 * at the ends face their rivals across the table. Seats still run clockwise:
 * near-left (6), far side 1-2-3, near-right (4), near-middle (5).
 */
internal val EMPEROR_PLACEMENT = Placement(near = listOf(5, 4, 3), far = listOf(0, 1, 2))

/** Emperor panels are bigger than the generals' beside them. */
internal const val EMPEROR_WEIGHT = 1.6f

/** A panel's share of its side: [EMPEROR_WEIGHT] for an emperor on an Emperor table, else 1. */
internal fun emperorWeight(emperorTable: Boolean, seat: Int): Float =
    if (emperorTable && Emperor.isEmperor(seat)) EMPEROR_WEIGHT else 1f

/** "Emperor · Team A" or "General · Team B": the role is always written, never only a colour. */
internal fun emperorRole(seat: Int): String =
    (if (Emperor.isEmperor(seat)) "Emperor" else "General") + " · Team ${Emperor.teamName(Emperor.team(seat))}"

/** "Emperor A" or "General B", the short tag beside a player's name. */
internal fun emperorTag(seat: Int): String =
    (if (Emperor.isEmperor(seat)) "Emperor " else "General ") + Emperor.teamName(Emperor.team(seat))

/** Team A and B tag colours; the tag's words name the team too. */
internal fun TurnHubPalette.emperorTeamColor(team: Int): Color = if (team == 0) info else warn

/** The gold an emperor's panel edge turns on their turn; generals keep the usual turn colour. */
internal val TurnHubPalette.emperorGold: Color get() = rarityRare

internal const val EMPEROR_MAP_HINT = "Seats 1 to 3 are Team A and 4 to 6 Team B. The middle seat of each team (2 and 5) is its emperor; " +
    "the generals beside them face the other team's generals. Drag or tap two seats to swap them."

/** "Attack: Bo · Reach: Bo, Cy": who this player can attack and affect now, as ranges close up when players leave. */
internal fun emperorRangeText(summary: TableSummary, playerNumber: Int, wide: Boolean): String {
    fun names(of: List<com.turnhub.android.domain.TablePlayer>) = of.joinToString(", ") { it.label }.ifEmpty { "nobody" }
    val attack = "Attack: ${names(summary.emperorTargets(playerNumber))}"
    return if (wide) "$attack · Reach: ${names(summary.emperorReach(playerNumber))}" else attack
}

/** Under the life total on an Emperor table: who the player can attack, and on wide panels who they reach. */
@Composable
internal fun EmperorRangeLine(unit: TableUnit, summary: TableSummary, width: Dp) {
    if (!summary.emperor || unit.out) return
    Text(
        emperorRangeText(summary, unit.members.first().playerNumber, width >= 300.dp),
        color = palette.muted,
        style = MaterialTheme.typography.labelMedium,
        maxLines = 1,
        overflow = TextOverflow.Ellipsis,
        modifier = Modifier.padding(top = 2.dp),
    )
}
