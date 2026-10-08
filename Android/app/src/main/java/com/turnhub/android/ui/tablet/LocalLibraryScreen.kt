package com.turnhub.android.ui.tablet

import androidx.activity.compose.BackHandler
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawingPadding
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.setValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.dp
import com.turnhub.android.standalone.DeliveryStatus
import com.turnhub.android.standalone.StandaloneState
import com.turnhub.android.standalone.GameRecord
import com.turnhub.android.standalone.MatchDelivery
import com.turnhub.android.standalone.LocalImportState
import com.turnhub.android.ui.components.ToneButton
import com.turnhub.android.ui.components.BrassCard
import com.turnhub.android.ui.components.tableBackground
import com.turnhub.android.ui.theme.palette
import java.text.DateFormat
import java.util.Date

/** Local facts and delivery states are presented separately, with text meaning. */
@Composable
fun LocalLibraryScreen(
    state: StandaloneState,
    onClose: () -> Unit,
    canImport: Boolean = false,
    importState: LocalImportState = LocalImportState(),
    onLoadProfiles: () -> Unit = {},
    onImport: (String, List<String>) -> Unit = { _, _ -> },
    onCancelImport: (String) -> Unit = {},
) {
    val p = palette
    BackHandler(onBack = onClose)
    Box(Modifier.fillMaxSize().tableBackground(p).safeDrawingPadding(), contentAlignment = Alignment.TopCenter) {
        LazyColumn(Modifier.widthIn(max = 720.dp).fillMaxWidth().padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(14.dp)) {
            item {
                Text("Device players and history", color = p.text, style = MaterialTheme.typography.headlineSmall,
                    modifier = Modifier.semantics { heading() })
                TextButton(onClick = onClose) { Text("Back") }
                Text("Results stay here after import. No games are removed automatically. Clearing app data or uninstalling removes them; backup/export is not available yet.", color = p.muted)
                state.storageProblem?.let { Text(it, color = p.text) }
                Text("Atlas statistics are separate. To import, connect and sign in, load its players, then explicitly map every player in a match. Names never choose an account.", color = p.muted)
                if (canImport) ToneButton("Load Atlas players for import", onLoadProfiles,
                    enabled = !importState.busy && state.storageProblem == null)
                
                if (importState.busy) Text("Contacting Atlas…", color = p.muted)
                importState.message?.let { Text(it, color = p.text) }
            }
            item { Text("Local players", color = p.text, modifier = Modifier.semantics { heading() }) }
            if (state.library.players.isEmpty()) item { Text("Add players in Play on this device. No account is needed.", color = p.muted) }
            items(state.library.players, key = { "player-${it.localId}" }) { player ->
                val totals = state.library.totals(player.localId)
                BrassCard {
                    Text(player.name, color = p.text)
                    Text("${totals.played} played · ${totals.won} won · ${totals.draws} draws", color = p.muted)
                }
            }
            item { Text("Finished games", color = p.text, modifier = Modifier.semantics { heading() }) }
            if (state.library.history.isEmpty()) item { Text("Finish a game to see its result here.", color = p.muted) }
            items(state.library.history.asReversed(), key = { "match-${it.recordId}" }) { record ->
                val delivery = state.library.delivery(record.recordId)
                BrassCard {
                    Text(DateFormat.getDateTimeInstance(DateFormat.MEDIUM, DateFormat.SHORT).format(Date(record.startedAtMs)), color = p.text)
                    Text("${record.profile.wireValue} · ${record.durationMs / 60_000} minutes · ${record.startingLife} starting life", color = p.muted)
                    Text(record.winner?.let { "Winner: ${record.players[it].name}" } ?: "Draw / no winner", color = p.text)
                    record.players.forEach { player ->
                        Text("${player.name}: ${player.finalLife} life, ${player.turnsCompleted} completed turns", color = p.muted)
                    }
                    val status = when (delivery.status) {
                        DeliveryStatus.NEEDS_LINKING -> "Local result · Atlas import needs explicit linking"
                        DeliveryStatus.PENDING -> "Atlas import pending"
                        DeliveryStatus.IMPORTED -> "Imported to Atlas"
                        DeliveryStatus.REJECTED -> "Atlas import needs attention"
                    }
                    Text(status, color = p.text)
                    
                    delivery.reason?.let { Text(it, color = p.muted) }
                    if (canImport && importState.atlasId != null && state.storageProblem == null &&
                        delivery.status != DeliveryStatus.IMPORTED) {
                        MatchImportForm(record, delivery, importState, onImport, onCancel)
                    }
                    
                }
            }
        }
    }
}

@Composable
private fun MatchImportForm(
    record: GameRecord,
    delivery: MatchDelivery,
    destination: LocalImportState,
    onImport: (String, List<String>) -> Unit,
    onCancel: (String) -> Unit,
) {
    val p = palette
    if (delivery.status == DeliveryStatus.PENDING) {
        Text("Waiting to import with the chosen player mapping.", color = p.muted)
        if (delivery.atlasId == destination.atlasId) {
            ToneButton("Retry import with this mapping", { onImport(record.recordId, delivery.profileIds) },
                enabled = !destination.busy)
        } else {
            Text("Reconnect to the Atlas this was linked to, or link it again for another Atlas.", color = p.muted)
            ToneButton("Cancel and link again", { onCancel(record.recordId) })
        }
        return
    }
    var editing by rememberSaveable(record.recordId) { mutableStateOf(false) }
    if (!editing) {
        ToneButton("Link this match for Atlas import", { editing = true }, enabled = !destination.busy)
        return
    }
    // Nothing is selected by label. Choices reset when the destination changes.
    var selected by rememberSaveable(record.recordId, destination.atlasId) {
        mutableStateOf(List(record.players.size) { "" })
    }
    record.players.forEachIndexed { index, player ->
        Text("Player ${index + 1}: ${player.name}", color = p.text)
        var expanded by rememberSaveable(record.recordId, destination.atlasId, index) { mutableStateOf(false) }
        Box {
            val choice = destination.profiles.firstOrNull { it.profileId == selected[index] }
            TextButton(onClick = { expanded = true }, enabled = !destination.busy) {
                Text(choice?.let { it.name } ?: "Choose Atlas profile for player ${index + 1}")
            }
            DropdownMenu(expanded = expanded, onDismissRequest = { expanded = false }) {
                destination.profiles.forEach { profile ->
                    DropdownMenuItem(text = { Text(profile.name) }, onClick = {
                        selected = selected.mapIndexed { i, old -> if (i == index) profile.profileId else old }
                        expanded = false
                    })
                }
            }
        }
    }
    Text("Import this match to the connected Atlas. Local identities and results stay on this device. Check every choice before importing.", color = p.muted)
    ToneButton("Import mapped match", { onImport(record.recordId, selected) }, enabled = !destination.busy &&
        selected.all { id -> destination.profiles.any { it.profileId == id } } && selected.distinct().size == selected.size)
    TextButton(onClick = { editing = false }) { Text("Cancel linking") }
}
