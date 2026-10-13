package com.turnhub.android.ui.tablet

import androidx.activity.compose.BackHandler
import androidx.compose.foundation.clickable
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
import com.turnhub.android.domain.TableClock
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
import kotlinx.coroutines.launch

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
    table: com.turnhub.android.standalone.StandaloneTable? = null,
    canLink: Boolean = false,
    onLinkAccount: (String, String?, String) -> Unit = { _, _, _ -> },
    onShareStats: (String) -> Unit = {},
    theme: com.turnhub.android.ui.theme.TurnHubThemeChoice = com.turnhub.android.ui.theme.TurnHubThemeChoice.AUTO,
    onThemeChosen: (com.turnhub.android.ui.theme.TurnHubThemeChoice) -> Unit = {},
) {
    val p = palette
    BackHandler(onBack = onClose)
    Box(Modifier.fillMaxSize().tableBackground(p).safeDrawingPadding(), contentAlignment = Alignment.TopCenter) {
        LazyColumn(Modifier.widthIn(max = 720.dp).fillMaxWidth().padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(14.dp)) {
            item {
                Text("Accounts and history", color = p.text, style = MaterialTheme.typography.headlineSmall,
                    modifier = Modifier.semantics { heading() })
                TextButton(onClick = onClose) { Text("Back") }
                Text("Results stay here after import. No games are removed automatically. Clearing app data or uninstalling removes them; backup/export is not available yet.", color = p.muted)
                state.storageProblem?.let { Text(it, color = p.text) }
                Text("Profiles, personalization and detailed results work offline. Link accounts once to import matching games automatically. Atlas verifies all imports; names never choose an account.", color = p.muted)
                if (canLink) ToneButton("Load Atlas accounts", onLoadProfiles,
                    enabled = !importState.busy && state.storageProblem == null)
                
                if (importState.busy) Text("Contacting Atlas…", color = p.muted)
                importState.message?.let { Text(it, color = p.text) }
                Text("Appearance", color = p.text)
                com.turnhub.android.ui.theme.TurnHubThemeChoice.entries.forEach { choice ->
                    TextButton({ onThemeChosen(choice) }) { Text("${if (theme == choice) "✓ " else ""}${choice.name.lowercase().replaceFirstChar { it.uppercase() }}") }
                }
                if (table != null) {
                    var name by rememberSaveable { mutableStateOf("") }
                    var secret by androidx.compose.runtime.remember { mutableStateOf("") }
                    var creating by androidx.compose.runtime.remember { mutableStateOf(false) }
                    val scope = androidx.compose.runtime.rememberCoroutineScope()
                    androidx.compose.material3.OutlinedTextField(name, { name = it.take(32) }, label = { Text("New account name") },
                        singleLine = true, modifier = Modifier.fillMaxWidth())
                    androidx.compose.material3.OutlinedTextField(secret, { secret = it.take(64) }, label = { Text("PIN or password (optional)") },
                        visualTransformation = androidx.compose.ui.text.input.PasswordVisualTransformation(), singleLine = true, modifier = Modifier.fillMaxWidth())
                    ToneButton(if (creating) "Creating account…" else "Create account", {
                        scope.launch {
                            creating = true
                            val credential = if (secret.isEmpty()) null else kotlinx.coroutines.withContext(kotlinx.coroutines.Dispatchers.Default) {
                                com.turnhub.android.standalone.LocalCredential.create(secret) }
                            val saved = table.createAccount(name, credential)
                            if (saved) { name = ""; secret = "" }; creating = false
                        }
                    }, enabled = !creating && name.isNotBlank() && state.storageProblem == null &&
                        (secret.isEmpty() || com.turnhub.android.data.ProfileSecret.isValid(secret)))
                    Text("A PIN protects account settings on this device. Shared-table play remains available. Use a 4–8 digit PIN or an 8–64 byte password; Atlas import needs one.", color = p.muted)
                }
            }
            item { Text("Local players", color = p.text, modifier = Modifier.semantics { heading() }) }
            if (state.library.players.isEmpty()) item { Text("Add players in Play on this device. No account is needed.", color = p.muted) }
            items(state.library.players, key = { "player-${it.localId}" }) { player ->
                val stats = state.library.stats(player.localId)
                val totals = stats.totals
                var open by rememberSaveable(player.localId) { mutableStateOf(false) }
                BrassCard(Modifier.clickable(onClickLabel = if (open) "Hide statistics" else "Show statistics") { open = !open }) {
                    Text(player.name, color = p.text)
                    val rate = if (totals.played > 0) " · ${totals.won * 100 / totals.played}% won" else ""
                    Text("${totals.played} played · ${totals.won} won · ${totals.draws} draws$rate", color = p.muted)
                    if (open && totals.played > 0) {
                        val f = TableClock::format
                        Text("Went first ${stats.started} · knocked out ${stats.eliminated} · conceded ${stats.conceded}", color = p.muted)
                        Text("${stats.turnsCompleted} turns · average ${f(stats.averageTurnMs)} · fastest ${f(stats.fastestTurnMs)} · longest ${f(stats.longestTurnMs)}", color = p.muted)
                        Text("Average game ${f(stats.averageGameMs)}", color = p.muted)
                        val full = state.library.profileStatistics(player.localId)
                        Text("Total game time ${f(full.lifetime.totalGameMs)} · total turn time ${f(full.lifetime.totalTurnMs)}", color = p.muted)
                        Text("Last game: ${full.lastGame.result} · ${f(full.lastGame.durationMs)} · ${full.lastGame.turns} turns", color = p.muted)
                        Text("Last turn average ${f(full.lastGame.averageTurnMs)} · fastest ${f(full.lastGame.fastestTurnMs)} · longest ${f(full.lastGame.longestTurnMs)}", color = p.muted)
                        ToneButton("Share statistics", { onShareStats("${player.name} · ${player.profileId}\n${full.winRate} win rate\n" +
                            "${totals.played} played, ${totals.won} won, ${totals.draws} draws\nWent first ${stats.started}, eliminated ${stats.eliminated}, conceded ${stats.conceded}\n" +
                            "${stats.turnsCompleted} turns, total ${f(full.lifetime.totalTurnMs)}, average ${f(stats.averageTurnMs)}, fastest ${f(stats.fastestTurnMs)}, longest ${f(stats.longestTurnMs)}\n" +
                            "Games total ${f(full.lifetime.totalGameMs)}, average ${f(stats.averageGameMs)}\nLast result ${full.lastGame.result}, duration ${f(full.lastGame.durationMs)}, turns ${full.lastGame.turns}\n" +
                            "Last turn average ${f(full.lastGame.averageTurnMs)}, fastest ${f(full.lastGame.fastestTurnMs)}, longest ${f(full.lastGame.longestTurnMs)}") })
                    } else if (totals.played > 0) {
                        Text("Tap for statistics", color = p.faint, style = MaterialTheme.typography.bodySmall)
                    }
                    if (open && table != null) LocalAccountEditor(player, table, importState, canLink, onLinkAccount)
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
                        Text("${player.name}: ${player.finalLife} life, ${player.turnsCompleted} completed turns" +
                            if (player.outOrder != null) (if (player.conceded) ", conceded" else ", eliminated") else "", color = p.muted)
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
                        MatchImportForm(record, delivery, importState, onImport, onCancelImport)
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
