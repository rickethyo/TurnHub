package com.turnhub.android.ui.home

import androidx.compose.foundation.border
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.MaterialTheme
import androidx.compose.ui.draw.clip
import androidx.compose.ui.unit.dp
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.ExposedDropdownMenuAnchorType
import androidx.compose.material3.ExposedDropdownMenuBox
import androidx.compose.material3.ExposedDropdownMenuDefaults
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import com.turnhub.android.data.PlayerSessionState
import com.turnhub.android.protocol.GameProfile
import com.turnhub.android.protocol.SessionInfo
import com.turnhub.android.protocol.TableState
import com.turnhub.android.ui.theme.palette

/** The signed-in session, if any. */
internal fun HomeUiState.sessionInfo(): SessionInfo? =
    (player?.session as? PlayerSessionState.SignedIn)?.info

/** A game that is being played (not being set up or finished). */
fun TableState.underway(): Boolean = this == TableState.STARTING || this == TableState.RUNNING || this == TableState.PAUSED

@Composable
internal fun ConfirmDialog(
    title: String,
    text: String,
    confirm: String,
    onConfirm: () -> Unit,
    onDismiss: () -> Unit,
) {
    val p = palette
    AlertDialog(
        onDismissRequest = onDismiss,
        containerColor = p.surface,
        title = { Text(title) },
        text = { Text(text, color = p.muted) },
        confirmButton = { TextButton(onClick = onConfirm) { Text(confirm, color = p.bad, fontWeight = FontWeight.Bold) } },
        dismissButton = { TextButton(onClick = onDismiss) { Text("Cancel", color = p.text) } },
    )
}

/** Life button steps: Yu-Gi-Oh! counts in hundreds, as on the portal, Atlas and the Sigils. */
internal data class LifeSteps(val small: Int, val big: Int)

internal fun lifeSteps(profile: GameProfile): LifeSteps =
    if (profile == GameProfile.YUGIOH) LifeSteps(100, 1000) else LifeSteps(1, 5)

@OptIn(ExperimentalMaterial3Api::class)
@Composable
internal fun <T> ChoiceDropdown(
    label: String,
    options: List<Pair<T, String>>,
    selected: T,
    onSelect: (T) -> Unit,
    modifier: Modifier = Modifier,
    enabled: Boolean = true,
) {
    var open by remember { mutableStateOf(false) }
    ExposedDropdownMenuBox(expanded = open, onExpandedChange = { if (enabled) open = it }, modifier = modifier) {
        OutlinedTextField(
            value = options.firstOrNull { it.first == selected }?.second ?: "",
            onValueChange = {},
            readOnly = true,
            enabled = enabled,
            label = { Text(label) },
            trailingIcon = { ExposedDropdownMenuDefaults.TrailingIcon(expanded = open) },
            modifier = Modifier.menuAnchor(ExposedDropdownMenuAnchorType.PrimaryNotEditable).fillMaxWidth(),
        )
        ExposedDropdownMenu(expanded = open, onDismissRequest = { open = false }) {
            options.forEach { (value, text) ->
                DropdownMenuItem(text = { Text(text) }, onClick = { onSelect(value); open = false })
            }
        }
    }
}

@Composable
internal fun EmptyNote(text: String) {
    val p = palette
    Text(
        text,
        color = p.muted,
        modifier = Modifier
            .fillMaxWidth()
            .clip(RoundedCornerShape(12.dp))
            .border(1.dp, p.lineStrong, RoundedCornerShape(12.dp))
            .padding(20.dp),
        style = MaterialTheme.typography.bodyMedium,
    )
}
