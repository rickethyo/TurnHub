package com.turnhub.android.ui.tablet

import androidx.activity.compose.BackHandler
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawingPadding
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.FilterChip
import androidx.compose.material3.FilterChipDefaults
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.getValue
import androidx.compose.runtime.setValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.LiveRegionMode
import androidx.compose.ui.semantics.liveRegion
import androidx.compose.ui.semantics.stateDescription
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.dp
import com.turnhub.android.data.ProfileSecret
import com.turnhub.android.domain.TableClock
import com.turnhub.android.standalone.LocalCredential
import com.turnhub.android.standalone.StandaloneTable
import com.turnhub.android.standalone.DeliveryStatus
import com.turnhub.android.standalone.StandaloneState
import com.turnhub.android.standalone.GameRecord
import com.turnhub.android.standalone.MatchDelivery
import com.turnhub.android.standalone.LocalImportState
import com.turnhub.android.ui.components.ToneButton
import com.turnhub.android.ui.components.displayName
import com.turnhub.android.ui.components.BrassCard
import com.turnhub.android.ui.components.tableBackground
import com.turnhub.android.ui.theme.palette
import com.turnhub.android.ui.components.DisclosureCard
import java.text.DateFormat
import java.util.Date
import kotlinx.coroutines.launch

enum class LibrarySection(val label: String) {
    PLAYERS("Players"), GAMES("History")
}

/** Local facts and delivery states are presented separately, with text meaning. */
@OptIn(ExperimentalLayoutApi::class)
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
    onBringPlayers: (String?, String?) -> Unit = { _, _ -> },
    onRestoreAccount: (String, String) -> Unit = { _, _ -> },
    embeddedSection: LibrarySection? = null,
    onStats: () -> Unit = {},
) {
    LaunchedEffect(canLink, importState.atlasId) { if (canLink && !importState.busy) onLoadProfiles() }
    val profiles = com.turnhub.android.domain.unifiedProfiles(state.library, importState.atlasId, if (canLink) importState.profiles else emptyList())
    val p = palette
    var section by rememberSaveable { mutableStateOf(LibrarySection.PLAYERS) }
    var creatingAccount by rememberSaveable { mutableStateOf(false) }
    val playersScroll = rememberLazyListState()
    val gamesScroll = rememberLazyListState()
    val shownSection = embeddedSection ?: section
    BackHandler(enabled = embeddedSection == null, onBack = onClose)
    var copyingAccount by remember { mutableStateOf(false) }
    if (copyingAccount && table != null) CopyAccountDialog(table, admin = false) { copyingAccount = false }
    if (creatingAccount && table != null) {
        CreateLocalAccountDialog(table, state.storageProblem == null) { creatingAccount = false }
    }
    Box(Modifier.fillMaxSize().tableBackground(p).then(if (embeddedSection == null) Modifier.safeDrawingPadding() else Modifier), contentAlignment = Alignment.TopCenter) {
        Column(Modifier.widthIn(max = 720.dp).fillMaxWidth()) {
            // Keep section navigation visible even with a long history or an open editor.
            if (embeddedSection == null) Column(Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 8.dp)) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Text("Accounts and history", color = p.text, style = MaterialTheme.typography.headlineSmall,
                        modifier = Modifier.weight(1f).semantics { heading() })
                    TextButton(onClick = onClose) { Text("Back") }
                }
                FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    LibrarySection.entries.forEach { entry ->
                        FilterChip(selected = section == entry, onClick = { section = entry },
                            label = { Text(entry.label) },
                            colors = FilterChipDefaults.filterChipColors(selectedContainerColor = p.accentSoft))
                    }
                }
            }
            LazyColumn(Modifier.weight(1f).fillMaxWidth(),
                state = when (shownSection) {
                    LibrarySection.PLAYERS -> playersScroll
                    LibrarySection.GAMES -> gamesScroll
                },
                contentPadding = PaddingValues(16.dp),
                verticalArrangement = Arrangement.spacedBy(14.dp)) {
                state.storageProblem?.let { problem ->
                    item(key = "storage-problem") {
                        Text(problem, color = p.warn, modifier = Modifier.semantics { liveRegion = LiveRegionMode.Polite })
                    }
                }
                if (importState.busy || importState.message != null) item(key = "atlas-feedback") {
                    Column(Modifier.semantics { liveRegion = LiveRegionMode.Polite }) {
                        if (importState.busy) Text("Contacting Atlas…", color = p.muted)
                        importState.message?.let { Text(it, color = p.text) }
                    }
                }
                when (shownSection) {
                    LibrarySection.PLAYERS -> {
                        item(key = "player-actions") {
                            if (table != null) ToneButton("Add player", { creatingAccount = true },
                                Modifier.fillMaxWidth(), enabled = state.storageProblem == null)
                            if (table != null) TextButton({ copyingAccount = true }, enabled = state.storageProblem == null) {
                                Text("Copy an account from another device") }
                            if (canLink) TextButton(onClick = onLoadProfiles,
                                enabled = !importState.busy && state.storageProblem == null) { Text("Refresh profiles") }
                        }
                        if (profiles.isEmpty()) item(key = "no-players") {
                            Text("Add a player to keep their name, appearance and stats for next time. You can also add guests directly in Game setup.", color = p.muted)
                        }
                        items(profiles, key = { it.key }) { entry ->
                            val player = entry.local
                            if (player == null) {
                                BrassCard {
                                    Text(entry.name, color = p.text, style = MaterialTheme.typography.titleMedium)
                                    Text("Lives on ${entry.location}", color = p.muted, style = MaterialTheme.typography.bodySmall)
                                    Text("ID ${entry.atlas!!.profileId}", color = p.faint, style = MaterialTheme.typography.labelSmall)
                                    if (table != null && importState.atlasId != null) TextButton(onClick = {
                                        table.referenceProfile(importState.atlasId, entry.atlas.profileId, entry.atlas.name)
                                    }) { Text("Use on this device") }
                                }
                            } else {
                            val totals = state.library.totals(player.localId)
                            var open by rememberSaveable(player.localId) { mutableStateOf(false) }
                            BrassCard {
                                // One compact row per player; Edit opens the settings in place.
                                Row(verticalAlignment = Alignment.CenterVertically) {
                                    Column(Modifier.weight(1f)) {
                                        Text(player.name, color = p.text, style = MaterialTheme.typography.titleMedium)
                                        Text("Lives on ${entry.location}", color = p.muted, style = MaterialTheme.typography.bodySmall)
                                        if (state.library.isDeviceAdmin(player.localId))
                                            Text("Device Admin", color = p.accent, style = MaterialTheme.typography.labelMedium)
                                        Text("${totals.played} played · ${totals.won} won · ${totals.draws} draws", color = p.muted,
                                            style = MaterialTheme.typography.bodySmall)
                                        Text("ID ${player.profileId}", color = p.faint, style = MaterialTheme.typography.labelSmall)
                                    }
                                    if (table != null) TextButton(onClick = { open = !open }, modifier = Modifier.semantics {
                                        contentDescription = if (open) "Done editing ${player.name}" else "Edit ${player.name}"
                                        stateDescription = if (open) "Expanded" else "Collapsed"
                                    }) { Text(if (open) "Done" else "Edit") }
                                }
                                if (table != null && open) LocalAccountEditor(player, table, importState, canLink, onLinkAccount)
                            }
                        }
                            }
                    }
                    LibrarySection.GAMES -> {
                        item(key = "statistics") {
                            ToneButton("Statistics", onStats, Modifier.fillMaxWidth())
                            // Every game as CSV, one row per player per game (the same file Atlas keeps).
                            if (state.library.history.isNotEmpty()) {
                                val context = androidx.compose.ui.platform.LocalContext.current
                                val csvScope = rememberCoroutineScope()
                                TextButton({ csvScope.launch {
                                    com.turnhub.android.ui.components.shareCsv(context, "turnhub-games.csv",
                                        com.turnhub.android.standalone.MatchCsv.of(state.library.history))
                                } }) { Text("Export all games (CSV)") }
                            }
                        }
                        val elderOnes = state.library.elderOneRecord()
                        if (elderOnes.isNotEmpty()) item(key = "elder-ones") {
                            BrassCard {
                                Text("Death May Die: against the Elder Ones", color = p.text, style = MaterialTheme.typography.titleMedium,
                                    modifier = Modifier.semantics { heading() })
                                elderOnes.forEach { Text("${it.elderOne}: ${it.won} won · ${it.lost} lost", color = p.text) }
                            }
                        }
                        if (state.library.history.isEmpty()) item(key = "no-games") {
                            BrassCard {
                                Text("No games yet", color = p.text, style = MaterialTheme.typography.titleMedium)
                                Text("Completed games appear here. Start a game from the Game tab.", color = p.muted)
                            }
                        }
                        if (canImport) item(key = "load-import-accounts") {
                            TextButton(onClick = onLoadProfiles, enabled = !importState.busy && state.storageProblem == null) {
                                Text("Refresh profiles")
                            }
                        }
                        items(state.library.history.asReversed(), key = { "match-${it.recordId}" }) { record ->
                            val delivery = state.library.delivery(record.recordId)
                            var open by rememberSaveable(record.recordId) { mutableStateOf(false) }
                            val canReviewImport = canImport && importState.atlasId != null && state.storageProblem == null &&
                                delivery.status != DeliveryStatus.IMPORTED && record.profile.atlasSupported
                            BrassCard {
                                val names = record.players.indices.filter(record::won).map { record.players[it].name }
                                Text(when {
                                    record.profile.deathMayDie -> if (record.winners.isEmpty()) "The investigators lost" else "The investigators won"
                                    names.isEmpty() -> "Draw / no winner"
                                    names.size == 1 -> "${names.single()} wins"
                                    else -> "${names.joinToString(" & ")} win"
                                }, color = p.text, style = MaterialTheme.typography.titleMedium)
                                Text(DateFormat.getDateTimeInstance(DateFormat.MEDIUM, DateFormat.SHORT).format(Date(record.startedAtMs)),
                                    color = p.muted, style = MaterialTheme.typography.bodySmall)
                                Text("${record.profile.displayName()}${if (record.twoHeadedGiant) " · Two-Headed Giant" else ""} · " +
                                    TableClock.format(record.durationMs), color = p.muted)
                                record.storyLine?.let { Text(it, color = p.text, style = MaterialTheme.typography.bodyMedium) }
                                Text(when (delivery.status) {
                                    DeliveryStatus.NEEDS_LINKING -> if (record.profile.atlasSupported) "Saved on this device"
                                        else "Saved on this device (Atlas doesn't import ${record.profile.displayName()} games yet)"
                                    DeliveryStatus.PENDING -> "Atlas import pending"
                                    DeliveryStatus.IMPORTED -> "Imported to Atlas"
                                    DeliveryStatus.REJECTED -> "Atlas import needs attention"
                                }, color = if (delivery.status == DeliveryStatus.REJECTED) p.warn else p.muted,
                                    style = MaterialTheme.typography.labelMedium)
                                TextButton(onClick = { open = !open }, modifier = Modifier.align(Alignment.End).semantics {
                                    stateDescription = if (open) "Expanded" else "Collapsed"
                                }) { Text(if (open) "Hide details" else if (canReviewImport) "Details and import" else "Details") }
                                if (open) {
                                    val score = when {
                                        record.profile.pokemon -> "prizes left"
                                        record.profile.lorcana -> "lore"
                                        record.profile.deathMayDie -> "wounds"
                                        else -> "life"
                                    }
                                    Text(when {
                                        record.profile.pokemon -> "${record.startingLife} Prize cards each"
                                        record.profile.lorcana -> "${record.startingLife} lore to win"
                                        record.profile.deathMayDie -> if (record.winners.isEmpty()) "Co-op · the investigators lost" else "Co-op · the investigators won"
                                        else -> "${record.startingLife} starting life"
                                    }, color = p.muted)
                                    record.players.forEachIndexed { index, player ->
                                        Text("${index + 1}. ${player.name}${player.investigator?.let { " ($it)" } ?: ""}: ${player.finalLife} $score · ${player.turnsCompleted} turns" +
                                            if (player.outOrder != null) (if (player.conceded) " · Conceded" else " · Eliminated") else "", color = p.text)
                                        if (player.selfEliminated) Text("Eliminated by self", color = p.muted)
                                        if (player.bagDrawn) Text("Drawn from the Bag of Doom", color = p.muted)
                                        player.eliminatedBy?.let { source -> record.players.getOrNull(source)?.let {
                                            Text("Eliminated by ${it.name}", color = p.muted)
                                        } }
                                        if (player.demerits > 0 || player.extensionsUsed > 0) {
                                            Text("${player.demerits} demerits · ${player.extensionsUsed} extensions used", color = p.muted)
                                        }
                                    }
                                    delivery.reason?.let { Text(it, color = p.muted) }
                                    if (canReviewImport) MatchImportForm(record, delivery, importState, onImport, onCancelImport)
                                }
                            }
                        }
                        item(key = "saved-games-help") {
                            DisclosureCard("About saved games") {
                                Text("Results stay on this device, including after Atlas import. Clearing app data or uninstalling removes them. Backup is not available yet.", color = p.muted)
                                Text("Link a player to an Atlas account in Edit player to import matching games automatically. Older games can be linked from Details and import.", color = p.muted)
                            }
                        }
                    }

                }
            }
        }
    }
}

@Composable
private fun CreateLocalAccountDialog(table: StandaloneTable, storageAvailable: Boolean, onDismiss: () -> Unit) {
    val p = palette
    var name by rememberSaveable { mutableStateOf("") }
    // Credentials never enter saved instance state.
    var secret by remember { mutableStateOf("") }
    var creating by remember { mutableStateOf(false) }
    var error by remember { mutableStateOf<String?>(null) }
    val scope = rememberCoroutineScope()
    val validSecret = secret.isEmpty() || ProfileSecret.isValid(secret)
    AlertDialog(
        onDismissRequest = { if (!creating) onDismiss() },
        containerColor = p.surface,
        title = { Text("Add player") },
        text = {
            Column(Modifier.verticalScroll(rememberScrollState()), verticalArrangement = Arrangement.spacedBy(12.dp)) {
                OutlinedTextField(name, { name = it.take(ProfileSecret.MAX_NAME_CHARS) }, label = { Text("Name") },
                    enabled = !creating, singleLine = true, modifier = Modifier.fillMaxWidth())
                OutlinedTextField(secret, { secret = it.take(ProfileSecret.MAX_CHARS) }, label = { Text("PIN or password (optional)") },
                    enabled = !creating, singleLine = true, modifier = Modifier.fillMaxWidth(),
                    keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Password),
                    visualTransformation = PasswordVisualTransformation(), isError = !validSecret,
                    supportingText = {
                        Text(if (validSecret) "Protects player settings. Play stays open to everyone."
                            else "Use a 4–8 digit PIN or an 8–64 byte password.")
                    })
                error?.let { Text(it, color = p.warn, modifier = Modifier.semantics { liveRegion = LiveRegionMode.Polite }) }
                if (!storageAvailable) Text("Storage is unavailable. Close this dialog to review the problem.", color = p.warn)
            }
        },
        confirmButton = {
            TextButton(enabled = !creating && storageAvailable && name.isNotBlank() && validSecret, onClick = {
                val submittedName = name.trim()
                val submittedSecret = secret
                creating = true
                error = null
                scope.launch {
                    try {
                        val credential = if (submittedSecret.isEmpty()) null else kotlinx.coroutines.withContext(kotlinx.coroutines.Dispatchers.Default) {
                            LocalCredential.create(submittedSecret)
                        }
                        if (table.createAccount(submittedName, credential)) {
                            secret = ""
                            onDismiss()
                        } else error = "Could not save this player. Please try again."
                    } finally {
                        creating = false
                    }
                }
            }) { Text(if (creating) "Saving…" else "Add player") }
        },
        dismissButton = { TextButton(onClick = onDismiss, enabled = !creating) { Text("Cancel") } },
    )
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
                Text(choice?.let { "${it.name} · ${it.profileId}" } ?: "Choose Atlas profile for player ${index + 1}")
            }
            DropdownMenu(expanded = expanded, onDismissRequest = { expanded = false }) {
                destination.profiles.forEach { profile ->
                    DropdownMenuItem(text = { Text("${profile.name} · ${profile.profileId}") }, onClick = {
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
