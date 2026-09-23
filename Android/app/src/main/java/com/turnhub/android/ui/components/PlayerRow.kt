package com.turnhub.android.ui.components

import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.turnhub.android.protocol.Player

/**
 * One player line inside [TableSummaryCard]. Life/elimination status is spelled
 * out as text; the active-player marker is a glyph plus bold weight, not color
 * alone (Documentation/engineering/ARCHITECTURAL_INVARIANTS.md, Invariant 11).
 */
@Composable
fun PlayerRow(
    player: Player,
    isActive: Boolean,
    modifier: Modifier = Modifier,
) {
    Row(modifier = modifier.fillMaxWidth().padding(vertical = 4.dp)) {
        val marker = if (isActive) "▶ " else "   "
        val name = player.displayName ?: "Player ${player.playerNumber}"
        Text(
            text = "$marker#${player.playerNumber} $name",
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
}
