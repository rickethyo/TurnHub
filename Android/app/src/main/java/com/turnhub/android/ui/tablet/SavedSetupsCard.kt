package com.turnhub.android.ui.tablet

import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.ui.Modifier
import androidx.compose.ui.semantics.*
import androidx.compose.ui.unit.dp
import com.turnhub.android.standalone.*
import com.turnhub.android.ui.components.*
import com.turnhub.android.ui.theme.palette

@Composable
internal fun SavedSetupsCard(state: StandaloneState, table: StandaloneTable) {
    var message by remember { mutableStateOf<String?>(null) }
    SavedSetupActions(state.library.gameSetups.map { it.name }, state.storageProblem == null,
        state.game.players.isNotEmpty(), "Stored on this device.", message,
        state.library.gameSetups.associate { it.name to "${it.game.profile.displayName()} · ${it.game.players.joinToString { player -> player.name }}" }) { op, name ->
        val ok = when (op) {
            "load" -> table.loadGameSetup(name)
            "delete" -> table.deleteGameSetup(name)
            else -> table.saveGameSetup(name, replace = op == "replace")
        }
        message = if (!ok) "Couldn't save that change. Check the saved players and available storage, then try again."
            else when (op) { "load" -> "Setup loaded. Check the players, then start when ready."; "delete" -> "Setup deleted."; else -> "Setup saved." }
    }
}

/** One interaction on the host and every controller; only the authority callback differs. */
@OptIn(ExperimentalLayoutApi::class)
@Composable
internal fun SavedSetupActions(names: List<String>, enabled: Boolean, canSave: Boolean, location: String,
    message: String? = null, summaries: Map<String, String> = emptyMap(), onAction: (String, String) -> Unit) {
    var saving by rememberSaveable { mutableStateOf(false) }
    var name by rememberSaveable { mutableStateOf("") }
    var pending by rememberSaveable { mutableStateOf<String?>(null) }
    var operation by rememberSaveable { mutableStateOf("") }
    DisclosureCard("Saved game setups · ${names.size}") {
        Text("Save this group, seat order, game and rules for next time. $location", color = palette.muted)
        ToneButton("Save current setup", { saving = true; name = "" }, Modifier.fillMaxWidth(), enabled = enabled && canSave)
        if (!canSave) Text("Add players before saving a setup.", color = palette.muted)
        Column(Modifier.heightIn(max = 280.dp).verticalScroll(rememberScrollState()), verticalArrangement = Arrangement.spacedBy(8.dp)) {
            names.forEach { saved ->
                Text(saved, style = MaterialTheme.typography.titleMedium)
                summaries[saved]?.let { Text(it, color = palette.muted, style = MaterialTheme.typography.bodySmall) }
                FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    ToneButton("Load setup", { pending = saved; operation = "load" }, enabled = enabled)
                    ToneButton("Delete setup", { pending = saved; operation = "delete" }, tone = Tone.BAD, enabled = enabled)
                }
            }
        }
        message?.let { Text(it, modifier = Modifier.semantics { liveRegion = LiveRegionMode.Polite }) }
    }
    if (saving) AlertDialog(onDismissRequest = { saving = false }, title = { Text("Save game setup") },
        text = { Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
            Text("Includes seated players, commanders, game type, life, timers and rules. Scores and history stay separate.")
            OutlinedTextField(name, { name = it.take(SavedGameSetup.MAX_NAME) }, label = { Text("Setup name") }, singleLine = true)
        } }, confirmButton = { ActionButton({
            saving = false
            if (names.any { it.equals(name.trim(), true) }) { pending = name.trim(); operation = "replace" }
            else onAction("save", name.trim())
        }, enabled = enabled && name.isNotBlank(), role = ActionRole.PRIMARY) { Text("Save setup") } },
        dismissButton = { ActionButton({ saving = false }) { Text("Cancel") } })
    pending?.let { chosen ->
        val action = when (operation) { "load" -> "Load setup"; "replace" -> "Replace setup"; else -> "Delete setup" }
        AlertDialog(onDismissRequest = { pending = null }, title = { Text("$action: $chosen?") },
            text = { Text(when (operation) {
                "load" -> "Replace the current players and rules with this setup? Saved players and history are kept."
                "replace" -> "Replace this saved setup with the current players and rules?"
                else -> "Remove this saved setup? Saved players and game history are kept."
            }) }, confirmButton = { ActionButton({ onAction(operation, chosen); pending = null }, enabled = enabled,
                role = if (operation == "delete" || operation == "replace") ActionRole.DESTRUCTIVE else ActionRole.PRIMARY) { Text(action) } },
            dismissButton = { ActionButton({ pending = null }) { Text("Cancel") } })
    }
}
