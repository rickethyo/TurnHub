package com.turnhub.android.ui.tablet

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.selection.toggleable
import androidx.compose.material3.FilterChip
import androidx.compose.material3.FilterChipDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.unit.dp
import com.turnhub.android.domain.TurnTimerStatus
import com.turnhub.android.standalone.StandaloneGame
import com.turnhub.android.ui.components.AccentButton
import com.turnhub.android.ui.theme.palette

// Setup pieces both full-screen tables share, so the game is set up the same
// way with or without Atlas.

/** The turn timer presets: Off, 1, 2, 3 and 5 minutes (GAMEPLAY.md). */
@OptIn(ExperimentalLayoutApi::class)
@Composable
internal fun TurnTimerChips(selectedMs: Long, enabled: Boolean, onSelect: (Long) -> Unit) {
    val p = palette
    Text("Turn timer", color = p.muted, style = MaterialTheme.typography.labelLarge)
    FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
        StandaloneGame.TURN_TIMER_PRESETS_MS.forEach { ms ->
            FilterChip(
                selected = selectedMs == ms,
                onClick = { onSelect(ms) },
                label = { Text(TurnTimerStatus.settingLabel(ms)) },
                enabled = enabled,
                colors = FilterChipDefaults.filterChipColors(selectedContainerColor = p.accentSoft),
            )
        }
    }
    Text("Running out never passes the turn: the clock counts the overtime until the player passes.",
        color = p.faint, style = MaterialTheme.typography.bodySmall)
}

/** A labelled switch row; the whole row toggles. */
@Composable
internal fun SetupSwitch(title: String, detail: String?, checked: Boolean, enabled: Boolean, onChange: (Boolean) -> Unit) {
    val p = palette
    Row(
        Modifier.fillMaxWidth().toggleable(value = checked, enabled = enabled, role = Role.Switch, onValueChange = onChange),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Column(Modifier.weight(1f)) {
            Text(title, color = p.text)
            detail?.let { Text(it, color = p.muted, style = MaterialTheme.typography.bodySmall) }
        }
        Switch(checked = checked, onCheckedChange = null, enabled = enabled)
    }
}

/** The one big start button. */
@Composable
internal fun StartTableButton(players: Int, enabled: Boolean, onStart: () -> Unit) =
    AccentButton(
        if (players < 2) "Add two players to start" else "Start full-screen game",
        onStart,
        Modifier.fillMaxWidth().heightIn(min = 64.dp),
        enabled = enabled && players >= 2,
    )

/** How the seat map is explained in both lobbies. */
internal const val SEAT_MAP_HINT = "This is how the table will look. Drag a player onto another seat to swap them, " +
    "or tap two seats. Turns go clockwise in number order."
