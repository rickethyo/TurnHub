package com.turnhub.android.ui.components

import androidx.compose.material3.ListItem
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import com.turnhub.android.protocol.Sigil

/**
 * One row in the paired-Sigil list. Connection state is spelled out in text,
 * not conveyed by color/icon alone (same accessibility reasoning as
 * [ConnectionStateBadge]).
 */
@Composable
fun SigilListItem(
    sigil: Sigil,
    modifier: Modifier = Modifier,
) {
    ListItem(
        modifier = modifier,
        headlineContent = { Text(sigil.name) },
        supportingContent = {
            val assignment = sigil.assignedPlayerNumber?.let { "Assigned to player #$it" } ?: "Unassigned"
            Text("Module ${sigil.moduleId} • $assignment")
        },
        trailingContent = {
            Text(
                text = if (sigil.connected) "Online" else "Offline",
                style = MaterialTheme.typography.labelLarge,
                color = if (sigil.connected) {
                    MaterialTheme.colorScheme.primary
                } else {
                    MaterialTheme.colorScheme.onSurfaceVariant
                },
            )
        },
    )
}
