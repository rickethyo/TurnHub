package com.turnhub.android.ui.components

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Card
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.semantics.LiveRegionMode
import androidx.compose.ui.semantics.liveRegion
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.turnhub.android.domain.TableClock
import com.turnhub.android.domain.TableSummary
import com.turnhub.android.protocol.GameProfile
import com.turnhub.android.protocol.TableState

/**
 * Table-level status: state, format, clocks and anything Atlas is waiting on.
 * Clocks are Atlas's sampled values rendered forward by [nowMs] between polls
 * ([TableClock]); nothing here decides game state.
 */
@Composable
fun TableHeader(
    summary: TableSummary,
    nowMs: Long,
    labelFor: (Int) -> String,
    modifier: Modifier = Modifier,
) {
    Card(modifier = modifier.fillMaxWidth()) {
        Column(modifier = Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
            Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                StateChip(summary.state)
                Text(
                    text = "${summary.settings.profile.displayName()} • starting life ${summary.settings.startingLife}",
                    style = MaterialTheme.typography.bodyMedium,
                )
            }

            if (summary.state == TableState.RUNNING || summary.state == TableState.PAUSED ||
                summary.state == TableState.GAME_OVER
            ) {
                Row(horizontalArrangement = Arrangement.spacedBy(24.dp)) {
                    Clock(label = "Game", value = TableClock.format(TableClock.gameElapsedMs(summary, nowMs)))
                    summary.activePlayerNumber?.let {
                        Clock(label = "Turn", value = TableClock.format(TableClock.turnElapsedMs(summary, nowMs)))
                    }
                }
            }

            summary.activePlayerNumber?.let {
                Text("Now playing: ${labelFor(it)}", style = MaterialTheme.typography.titleMedium)
            }
            summary.winnerPlayerNumber?.let {
                Text("Winner: ${labelFor(it)}", style = MaterialTheme.typography.titleMedium, fontWeight = FontWeight.Bold)
            }

            PendingBanners(summary, nowMs, labelFor)

            Text(
                text = "${summary.atlasId} • firmware ${summary.firmwareVersion} • rev ${summary.revision}",
                style = MaterialTheme.typography.labelSmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
        }
    }
}

@Composable
private fun PendingBanners(summary: TableSummary, nowMs: Long, labelFor: (Int) -> String) {
    val pending = summary.pending
    val messages = buildList {
        pending.passPlayer?.let {
            val seconds = (TableClock.passGraceRemainingMs(summary, nowMs) + 999) / 1000
            add("${labelFor(it)} passed – ${seconds}s left to cancel")
        }
        pending.winClaimPlayer?.let { claimant ->
            val confirming = pending.winConfirmationPlayer?.let { " – waiting for ${labelFor(it)} to confirm" }.orEmpty()
            add("${labelFor(claimant)} claims the win$confirming")
        }
        pending.eliminationTargetPlayer?.let { add("Eliminating ${labelFor(it)}?") }
        if (summary.state == TableState.STARTING) add("Game starting…")
        if (summary.state == TableState.PAUSED) add("Paused")
    }
    messages.forEach { message ->
        Text(
            text = message,
            style = MaterialTheme.typography.bodyLarge,
            fontWeight = FontWeight.SemiBold,
            color = MaterialTheme.colorScheme.onTertiaryContainer,
            modifier = Modifier
                .fillMaxWidth()
                .background(MaterialTheme.colorScheme.tertiaryContainer, RoundedCornerShape(8.dp))
                .padding(horizontal = 12.dp, vertical = 8.dp)
                .semantics { liveRegion = LiveRegionMode.Polite },
        )
    }
}

@Composable
private fun Clock(label: String, value: String) {
    Column {
        Text(label, style = MaterialTheme.typography.labelMedium, color = MaterialTheme.colorScheme.onSurfaceVariant)
        Text(value, style = MaterialTheme.typography.headlineMedium, fontWeight = FontWeight.SemiBold)
    }
}

/** State as a labeled chip: the text carries the meaning, color only reinforces it. */
@Composable
private fun StateChip(state: TableState) {
    val (label, color) = when (state) {
        TableState.LOBBY -> "Lobby" to MaterialTheme.colorScheme.secondary
        TableState.STARTING -> "Starting" to Color(0xFFB26A00)
        TableState.RUNNING -> "Running" to Color(0xFF2E7D32)
        TableState.PAUSED -> "Paused" to Color(0xFFB26A00)
        TableState.GAME_OVER -> "Game over" to MaterialTheme.colorScheme.primary
    }
    Text(
        text = label,
        style = MaterialTheme.typography.labelLarge,
        color = Color.White,
        modifier = Modifier
            .background(color, RoundedCornerShape(percent = 50))
            .padding(horizontal = 12.dp, vertical = 4.dp),
    )
}

fun GameProfile.displayName(): String = when (this) {
    GameProfile.GENERIC -> "Generic"
    GameProfile.MTG -> "Magic"
    GameProfile.MTG_COMMANDER -> "Commander"
    GameProfile.YUGIOH -> "Yu-Gi-Oh!"
}
