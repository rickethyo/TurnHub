package com.turnhub.android.ui.tablet

import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.ui.Modifier
import androidx.compose.ui.semantics.LiveRegionMode
import androidx.compose.ui.semantics.liveRegion
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.dp
import com.turnhub.android.standalone.StandaloneState
import com.turnhub.android.standalone.StandaloneTable
import com.turnhub.android.standalone.SavedGameSetup
import com.turnhub.android.ui.components.*
import com.turnhub.android.ui.theme.palette

/** Presets remain device-local and load only through the lobby's authority. */
@Composable
internal fun SavedSetupsCard(state: StandaloneState, table: StandaloneTable) {
    var saving by rememberSaveable { mutableStateOf(false) }
    var name by rememberSaveable { mutableStateOf("") }
    var pending by rememberSaveable { mutableStateOf<String?>(null) }
    var operation by rememberSaveable { mutableStateOf("") }
    var message by rememberSaveable { mutableStateOf<String?>(null) }
    val enabled = state.storageProblem == null
    fun result(ok: Boolean, success: String, failure: String = "Couldn't save that change. Check available storage and try again.") {
        message = if (ok) success else failure
    }
    DisclosureCard("Saved game setups · ${state.library.gameSetups.size}") {
        Text("Save this group, seat order, game and rules for next time. Stored on this device.", color = palette.muted)
        ToneButton("Save current setup", { saving = true; name = "" }, Modifier.fillMaxWidth(),
            enabled = enabled && state.game.players.isNotEmpty())
        if (state.game.players.isEmpty()) Text("Add players before saving a setup.", color = palette.muted)
        Column(Modifier.heightIn(max = 280.dp).verticalScroll(rememberScrollState()),
            verticalArrangement = Arrangement.spacedBy(8.dp)) {
            state.library.gameSetups.forEach { setup ->
                Text(setup.name, color = palette.text, style = MaterialTheme.typography.titleMedium)
                Text("${setup.game.profile.displayName()} · ${setup.game.players.joinToString { it.name }}",
                    color = palette.muted, style = MaterialTheme.typography.bodySmall)
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    ToneButton("Load", { pending = setup.name; operation = "load" }, enabled = enabled)
                    TextButton(onClick = { pending = setup.name; operation = "delete" }, enabled = enabled) { Text("Delete") }
                }
            }
        }
        message?.let { Text(it, color = palette.text, modifier = Modifier.semantics { liveRegion = LiveRegionMode.Polite }) }
    }
    if (saving) AlertDialog(onDismissRequest = { saving = false }, title = { Text("Save game setup") },
        text = { Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
            Text("Includes seated players, commanders, game type, life, timers and rules. Scores and history stay separate.")
            OutlinedTextField(name, { name = it.take(SavedGameSetup.MAX_NAME) }, label = { Text("Name, e.g. Friday Commander") }, singleLine = true)
        } },
        confirmButton = { TextButton(onClick = {
            saving = false
            if (state.library.gameSetups.any { it.name.equals(name.trim(), ignoreCase = true) }) {
                pending = name.trim(); operation = "replace"
            } else result(table.saveGameSetup(name), "Setup saved.")
        }, enabled = enabled && name.isNotBlank()) { Text("Save setup") } },
        dismissButton = { TextButton(onClick = { saving = false }) { Text("Cancel") } })
    pending?.let { chosen ->
        AlertDialog(onDismissRequest = { pending = null }, title = { Text(when (operation) {
            "load" -> "Load $chosen?"
            "replace" -> "Replace $chosen?"
            else -> "Delete $chosen?"
        }) }, text = { Text(when (operation) {
            "load" -> "Replaces the players and settings in this setup. Saved players and game history are kept."
            "replace" -> "Updates the saved setup with the players and settings currently on screen."
            else -> "Removes this saved setup. Saved players and game history are kept."
        }) }, confirmButton = { TextButton(onClick = {
            when (operation) {
                "load" -> result(table.loadGameSetup(chosen), "Setup loaded. Check the players, then start when ready.",
                    "Couldn't load this setup. If a saved player was deleted, add them again and save a new setup. Otherwise check available storage.")
                "replace" -> result(table.saveGameSetup(chosen, replace = true), "Setup updated.")
                else -> result(table.deleteGameSetup(chosen), "Setup deleted.")
            }
            pending = null
        }, enabled = enabled) { Text(when (operation) { "load" -> "Load setup"; "replace" -> "Replace setup"; else -> "Delete setup" }) } },
            dismissButton = { TextButton(onClick = { pending = null }) { Text("Cancel") } })
    }
}
