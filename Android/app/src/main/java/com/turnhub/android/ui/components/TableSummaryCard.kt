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
import com.turnhub.android.domain.TableSummary
import com.turnhub.android.protocol.GameProfile
import com.turnhub.android.protocol.TableState

/**
 * A read-only summary of the live table. This card only renders what Atlas
 * reported in its latest snapshot; it performs no gameplay logic of its own
 * (Documentation/engineering/ARCHITECTURAL_INVARIANTS.md, Invariant 1).
 */
@Composable
fun TableSummaryCard(
    summary: TableSummary,
    modifier: Modifier = Modifier,
) {
    val labels = summary.players.associate { it.playerNumber to it.label }
    val labelFor: (Int) -> String = { number -> labels[number] ?: "Player $number" }

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
                val clock = if (summary.state == TableState.RUNNING || summary.state == TableState.PAUSED) {
                    " • turn ${formatElapsed(summary.turnElapsedMs)}"
                } else {
                    ""
                }
                Text(text = "Active: ${labelFor(active)}$clock", style = MaterialTheme.typography.bodyMedium)
            }
            summary.starterPlayerNumber?.let {
                Text(text = "Starting player: ${labelFor(it)}", style = MaterialTheme.typography.bodyMedium)
            }
            summary.winnerPlayerNumber?.let {
                Text(text = "Winner: ${labelFor(it)}", style = MaterialTheme.typography.bodyMedium)
            }
            summary.pending.passPlayer?.let {
                Text(text = "Pass pending from ${labelFor(it)}", style = MaterialTheme.typography.bodyMedium)
            }
            summary.pending.winClaimPlayer?.let {
                Text(text = "Win claimed by ${labelFor(it)}", style = MaterialTheme.typography.bodyMedium)
            }

            if (summary.players.isEmpty()) {
                Spacer(modifier = Modifier.height(12.dp))
                Text(
                    text = "No players have joined.",
                    style = MaterialTheme.typography.bodyMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            } else {
                Spacer(modifier = Modifier.height(12.dp))
                summary.players.forEach { player ->
                    PlayerRow(
                        player = player,
                        isActive = player.playerNumber == summary.activePlayerNumber,
                        labelFor = labelFor,
                    )
                }
            }
        }
    }
}

/** Renders an Atlas-sampled duration; never advanced locally. */
private fun formatElapsed(ms: Long): String {
    val totalSeconds = ms / 1000
    val hours = totalSeconds / 3600
    val minutes = (totalSeconds % 3600) / 60
    val seconds = totalSeconds % 60
    return if (hours > 0) "%d:%02d:%02d".format(hours, minutes, seconds) else "%d:%02d".format(minutes, seconds)
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
