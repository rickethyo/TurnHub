package com.turnhub.android.ui.components

import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Card
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import com.turnhub.android.protocol.GameProfile
import com.turnhub.android.protocol.TableState
import com.turnhub.android.protocol.TableSummary

/**
 * A read-only summary of the current table, derived from a mocked
 * state-v0.1 snapshot (see [TableSummary]). This card only renders what Atlas
 * reports; it performs no gameplay logic of its own
 * (Documentation/engineering/ARCHITECTURAL_INVARIANTS.md, Invariant 1).
 */
@Composable
fun TableSummaryCard(
    summary: TableSummary,
    modifier: Modifier = Modifier,
) {
    Card(modifier = modifier.fillMaxWidth()) {
        Column(modifier = Modifier.padding(16.dp)) {
            Text(text = summary.atlasId, style = MaterialTheme.typography.titleMedium)
            Text(
                text = "Firmware ${summary.firmwareVersion} • rev ${summary.revision}",
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )

            Spacer(modifier = Modifier.height(12.dp))

            Text(text = "Table: ${summary.state.toDisplayName()}", style = MaterialTheme.typography.bodyLarge)
            Text(
                text = "Format: ${summary.settings.profile.toDisplayName()} • " +
                    "Starting life ${summary.settings.startingLife}",
                style = MaterialTheme.typography.bodyMedium,
            )
            summary.activePlayerNumber?.let { active ->
                Text(text = "Active player: #$active", style = MaterialTheme.typography.bodyMedium)
            }

            if (summary.players.isNotEmpty()) {
                Spacer(modifier = Modifier.height(12.dp))
                summary.players.forEach { player ->
                    PlayerRow(
                        player = player,
                        isActive = player.playerNumber == summary.activePlayerNumber,
                    )
                }
            }
        }
    }
}

private fun TableState.toDisplayName(): String = when (this) {
    TableState.LOBBY -> "Lobby"
    TableState.STARTING -> "Starting"
    TableState.RUNNING -> "Running"
    TableState.PAUSED -> "Paused"
    TableState.GAME_OVER -> "Game over"
}

private fun GameProfile.toDisplayName(): String = when (this) {
    GameProfile.GENERIC -> "Generic"
    GameProfile.MTG -> "Magic"
    GameProfile.MTG_COMMANDER -> "Commander"
    GameProfile.YUGIOH -> "Yu-Gi-Oh!"
}
