package com.turnhub.android.ui.components

import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.turnhub.android.domain.ControllerHandle
import com.turnhub.android.domain.TablePlayer
import com.turnhub.android.domain.seatLabel
import com.turnhub.android.protocol.LifeRequestState

/**
 * One player inside [TableSummaryCard]. Status is spelled out in text; the
 * active-player marker is a glyph plus bold weight, not color alone
 * (Documentation/engineering/ARCHITECTURAL_INVARIANTS.md, Invariant 11).
 * Everything shown is exactly what Atlas reported.
 */
@Composable
fun PlayerRow(
    player: TablePlayer,
    isActive: Boolean,
    labelFor: (Int) -> String,
    modifier: Modifier = Modifier,
) {
    Column(modifier = modifier.fillMaxWidth().padding(vertical = 4.dp)) {
        Row(modifier = Modifier.fillMaxWidth()) {
            val marker = if (isActive) "▶ " else "   "
            Text(
                text = "$marker#${player.playerNumber} ${player.label}",
                style = MaterialTheme.typography.bodyMedium,
                fontWeight = if (isActive) FontWeight.Bold else FontWeight.Normal,
                modifier = Modifier.weight(1f),
            )
            val status = when {
                player.eliminated -> "Eliminated"
                player.life != null -> "${player.life} life"
                else -> "—"
            }
            Text(text = status, style = MaterialTheme.typography.bodyMedium)
        }
        val controller = when (player.controller.kind) {
            ControllerHandle.Kind.PHYSICAL -> "Sigil ${player.controller.id}, seat ${seatLabel(player.slot)}"
            ControllerHandle.Kind.VIRTUAL -> "Phone/browser"
            ControllerHandle.Kind.UNKNOWN -> "Controller ${player.controller.id}"
        }
        Text(
            text = "   $controller • ${player.turnsCompleted} turns completed",
            style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
        player.commanderDamage.forEach { entry ->
            Text(
                text = "   Commander damage from ${labelFor(entry.sourcePlayer)}: " +
                    entry.damage.joinToString(" / "),
                style = MaterialTheme.typography.bodySmall,
            )
        }
        player.lifeRequest?.takeIf { it.state == LifeRequestState.PENDING }?.let { request ->
            val sign = if (request.delta > 0) "+" else ""
            Text(
                text = "   Life change $sign${request.delta} requested by ${labelFor(request.actor)} " +
                    "– awaiting ${labelFor(request.target)}",
                style = MaterialTheme.typography.bodySmall,
            )
        }
    }
}
