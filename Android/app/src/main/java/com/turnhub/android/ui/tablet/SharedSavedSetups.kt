package com.turnhub.android.ui.tablet

import androidx.compose.foundation.layout.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.ui.Modifier
import com.turnhub.android.lan.*
import com.turnhub.android.ui.components.*
import com.turnhub.android.ui.theme.palette

@Composable
internal fun SharedSavedSetups(state: LanUiState, model: LanViewModel) {
    val game = state.remote ?: return
    val enabled = state.online && !state.busy && !state.uncertain && (state.role in setOf("player", "table") || state.master)
    var name by rememberSaveable { mutableStateOf("") }
    var pending by remember { mutableStateOf<Pair<String,String>?>(null) }
    pending?.let { (op, selected) -> AlertDialog(onDismissRequest = { pending = null },
        title = { Text(when (op) { "load-setup" -> "Load $selected?"; "save-setup" -> "Replace $selected?"; else -> "Delete $selected?" }) },
        text = { Text(when (op) { "load-setup" -> "Replace the current setup with this saved group and rules?"; "save-setup" -> "Replace the saved group and rules with the current setup?"; else -> "Remove this saved setup? Saved games and players are kept." }) },
        confirmButton = { TextButton({ model.setup(game, op, "name" to selected, "replace" to (op == "save-setup")); pending = null }, enabled = enabled) { Text("Confirm") } },
        dismissButton = { TextButton({ pending = null }) { Text("Cancel") } }) }
    DisclosureCard("Saved game setups · ${state.setupNames.size}") {
        Text("Save this group, seat order, game and rules for next time. Stored on the host device.", color = palette.muted)
        OutlinedTextField(name, { name = it.take(64) }, label = { Text("Setup name") }, singleLine = true)
        ToneButton("Save current setup", { if (state.setupNames.any { it.equals(name.trim(), true) }) pending = "save-setup" to name.trim() else model.setup(game, "save-setup", "name" to name) }, enabled = enabled && name.isNotBlank())
        state.setupNames.forEach { saved -> Row(Modifier.fillMaxWidth()) {
            Text(saved, modifier = Modifier.weight(1f), color = palette.text)
            TextButton({ pending = "load-setup" to saved }, enabled = enabled) { Text("Load") }
            TextButton({ pending = "delete-setup" to saved }, enabled = enabled) { Text("Delete") }
        } }
    }
}
