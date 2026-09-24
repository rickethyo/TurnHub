package com.turnhub.android.ui.components

import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.semantics.clearAndSetSemantics
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextDecoration
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import com.turnhub.android.domain.ControllerHandle
import com.turnhub.android.domain.TableClock
import com.turnhub.android.domain.TablePlayer
import com.turnhub.android.domain.TableSummary
import com.turnhub.android.domain.seatLabel
import com.turnhub.android.protocol.LifeRequestState

/**
 * One player at the table: name, a large life total and status. Active,
 * winner and eliminated states are spelled out in text; the thicker border
 * and dimming only reinforce them (ARCHITECTURAL_INVARIANTS.md, Invariant 11).
 * Screen readers get one merged description per card.
 */
@Composable
fun PlayerCard(
    player: TablePlayer,
    summary: TableSummary,
    nowMs: Long,
    labelFor: (Int) -> String,
    modifier: Modifier = Modifier,
) {
    val isActive = player.playerNumber == summary.activePlayerNumber
    val isWinner = player.playerNumber == summary.winnerPlayerNumber
    val status = when {
        isWinner -> "🏆 Winner"
        player.eliminated -> "Eliminated"
        isActive -> "▶ Now playing • ${TableClock.format(TableClock.turnElapsedMs(summary, nowMs))}"
        else -> null
    }
    val controller = when (player.controller.kind) {
        ControllerHandle.Kind.PHYSICAL -> "Sigil ${player.controller.id} • seat ${seatLabel(player.slot)}"
        ControllerHandle.Kind.VIRTUAL -> "Phone / browser"
        ControllerHandle.Kind.UNKNOWN -> "Controller ${player.controller.id}"
    }
    // Lobby life is null until Atlas captures it at start; preview Atlas's
    // configured starting life instead, visibly muted and labeled as such.
    val lifeText = player.life?.toString() ?: summary.settings.startingLife.toString()
    val lifeLabel = if (player.life == null) "starting life" else "life"
    val extras = buildList {
        player.commanderDamage.forEach { entry ->
            add("Cmdr from ${labelFor(entry.sourcePlayer)}: ${entry.damage.joinToString(" / ")}")
        }
        player.lifeRequest?.takeIf { it.state == LifeRequestState.PENDING }?.let { request ->
            val sign = if (request.delta > 0) "+" else ""
            add("$sign${request.delta} life requested by ${labelFor(request.actor)}")
        }
    }
    val description = buildString {
        append(player.label)
        status?.let { append(", ").append(it.removePrefix("▶ ").removePrefix("🏆 ")) }
        append(", $lifeText $lifeLabel")
        append(", $controller, ${player.turnsCompleted} turns completed")
        extras.forEach { append(", ").append(it) }
    }

    Card(
        modifier = modifier
            .fillMaxWidth()
            .alpha(if (player.eliminated) 0.6f else 1f)
            .clearAndSetSemantics { contentDescription = description },
        border = when {
            isActive || isWinner -> BorderStroke(3.dp, MaterialTheme.colorScheme.primary)
            else -> null
        },
        colors = if (isActive) {
            CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.primaryContainer)
        } else {
            CardDefaults.cardColors()
        },
    ) {
        Column(
            modifier = Modifier.padding(12.dp).fillMaxWidth(),
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.spacedBy(2.dp),
        ) {
            Text(
                text = player.label,
                style = MaterialTheme.typography.titleMedium,
                fontWeight = FontWeight.SemiBold,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
                textDecoration = if (player.eliminated) TextDecoration.LineThrough else null,
            )
            Text(
                text = lifeText,
                style = MaterialTheme.typography.displayLarge,
                fontWeight = FontWeight.Bold,
                color = if (player.life == null) {
                    MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.6f)
                } else {
                    MaterialTheme.colorScheme.onSurface
                },
            )
            Text(
                text = lifeLabel,
                style = MaterialTheme.typography.labelMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
            status?.let {
                Text(it, style = MaterialTheme.typography.labelLarge, fontWeight = FontWeight.Bold, textAlign = TextAlign.Center)
            }
            Text(
                text = "$controller • ${player.turnsCompleted} turns",
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                textAlign = TextAlign.Center,
            )
            extras.forEach {
                Text(it, style = MaterialTheme.typography.bodySmall, textAlign = TextAlign.Center)
            }
        }
    }
}
