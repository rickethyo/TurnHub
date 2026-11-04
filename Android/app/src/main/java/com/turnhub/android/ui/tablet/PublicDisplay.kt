package com.turnhub.android.ui.tablet

import android.content.Context
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.selection.selectable
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.RadioButton
import androidx.compose.material3.Text
import androidx.compose.runtime.*
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.unit.dp
import com.turnhub.android.lan.LanUiState
import com.turnhub.android.lan.LanViewModel
import com.turnhub.android.lan.RoomDisplay
import com.turnhub.android.ui.components.ToneButton
import com.turnhub.android.ui.theme.palette

internal data class PublicDisplayChoice(val upright: Boolean, val enabled: Boolean, val id: String,
    val targets: List<RoomDisplay>, val planesAvailable: Boolean = false,
    val setView: (String, String) -> Unit = { _, _ -> }, val set: (String, Boolean) -> Unit)
internal val LocalPublicDisplay = staticCompositionLocalOf {
    PublicDisplayChoice(false, false, "local", emptyList()) { _, _ -> }
}

/** Independent presentation choices for each device; local-only when no room is open. */
@Composable
internal fun PublicDisplayScope(state: LanUiState, model: LanViewModel, content: @Composable () -> Unit) {
    val context = LocalContext.current
    val prefs = remember(context) { context.getSharedPreferences("turnhub_ui", Context.MODE_PRIVATE) }
    var local by remember { mutableStateOf(prefs.getBoolean("display_all_upright", false)) }
    val shared = state.host != null || state.joined
    val enabled = state.host != null || !shared || state.online && !state.busy && !state.uncertain &&
        (state.role in setOf("player", "investigator", "table") || state.master)
    val id = if (shared) state.displayId else "local"
    val choice = PublicDisplayChoice(if (shared) state.displayAllUpright else local, enabled, id,
        if (shared) state.displayTargets else listOf(RoomDisplay(id, "This screen", local)),
        planesAvailable = state.remote?.planechase?.enabled == true ||
            (state.host != null && model.hostPlanesAvailable()),
        setView = model::setDisplayView) { target, value ->
        if (shared) model.setDisplayAllUpright(target, value)
        else { local = value; prefs.edit().putBoolean("display_all_upright", value).apply() }
    }
    CompositionLocalProvider(LocalPublicDisplay provides choice, content = content)
}

@Composable
internal fun PublicDisplayButton() {
    var open by remember { mutableStateOf(false) }
    ToneButton("Display settings", { open = true }, Modifier.fillMaxWidth())
    if (open) PublicDisplayDialog { open = false }
}

@Composable
internal fun PublicDisplayDialog(onClose: () -> Unit) {
    val choice = LocalPublicDisplay.current
    val p = palette
    // Start with the dedicated public display; players can explicitly choose another screen.
    var target by rememberSaveable(choice.id) {
        mutableStateOf(choice.targets.firstOrNull { it.public && it.id != "host" }?.id ?: choice.id)
    }
    val selected = choice.targets.firstOrNull { it.id == target } ?: choice.targets.firstOrNull { it.id == choice.id }
    var picking by remember { mutableStateOf(false) }
    AlertDialog(onDismissRequest = onClose, title = { Text("Display settings") },
        text = {
            Column(Modifier.verticalScroll(rememberScrollState()), verticalArrangement = Arrangement.spacedBy(12.dp)) {
                Text("Screen to change", color = p.muted, style = MaterialTheme.typography.labelLarge)
                Text(selected?.name ?: "This screen", color = p.text, style = MaterialTheme.typography.titleLarge)
                selected?.let { display ->
                    Text(listOf(display.detail, "This screen".takeIf { display.id == choice.id }).filterNotNull()
                        .filter { it.isNotBlank() }.joinToString(" · "), color = p.muted)
                }
                if (choice.targets.size > 1) ToneButton("Choose another screen", { picking = true }, Modifier.fillMaxWidth())
                val enabled = choice.enabled && selected != null
                if (selected?.view != null) {
                    Text("Show on this screen", color = p.muted, style = MaterialTheme.typography.labelLarge)
                    DisplayDirection("Table", "Player cards and turn timing", selected.view == "table", enabled) {
                        choice.setView(selected.id, "table")
                    }
                    DisplayDirection("Planes", if (choice.planesAvailable) "The current plane and planar details" else "Enable Planechase in game setup",
                        selected.view == "planes", enabled && choice.planesAvailable) {
                        choice.setView(selected.id, "planes")
                    }
                }
                Text("Table panel direction", color = p.muted, style = MaterialTheme.typography.labelLarge)
                DisplayDirection("All panels upright", "For a TV or wall display", selected?.upright == true, enabled) {
                    selected?.let { choice.set(it.id, true) }
                }
                DisplayDirection("Face each player", "For a screen in the middle of the table", selected?.upright != true, enabled) {
                    selected?.let { choice.set(it.id, false) }
                }
                Text("Only this selected screen changes.", color = p.muted, style = MaterialTheme.typography.bodySmall)
            }
        }, confirmButton = { ToneButton("Done", onClose) })
    if (picking) AlertDialog(onDismissRequest = { picking = false }, title = { Text("Choose a screen") },
        text = {
            Column(Modifier.verticalScroll(rememberScrollState()), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                val sorted = choice.targets.sortedBy { if (it.public && it.id != "host") 0 else if (it.id == "host") 1 else 2 }
                sorted.forEach { display ->
                    DisplayDirection(display.name,
                        listOf(display.detail, "This screen".takeIf { display.id == choice.id }).filterNotNull()
                            .filter { it.isNotBlank() }.joinToString(" · "),
                        display.id == selected?.id, true) { target = display.id; picking = false }
                }
            }
        }, confirmButton = { ToneButton("Cancel", { picking = false }) })
}

@Composable
private fun DisplayDirection(title: String, detail: String, selected: Boolean, enabled: Boolean, onClick: () -> Unit) {
    val p = palette
    Row(Modifier.fillMaxWidth().selectable(selected = selected, enabled = enabled, role = Role.RadioButton, onClick = onClick)
        .padding(vertical = 8.dp), verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(12.dp)) {
        RadioButton(selected, onClick = null, enabled = enabled)
        Column(Modifier.weight(1f)) {
            Text(title, color = p.text, style = MaterialTheme.typography.titleMedium)
            if (detail.isNotBlank()) Text(detail, color = p.muted, style = MaterialTheme.typography.bodyMedium)
        }
    }
}

internal fun publicPanelDegrees(upright: Boolean, facing: Int, turn: Int): Int =
    if (upright) 0 else Math.floorMod(facing + turn, 360)
