package com.turnhub.android.ui.tablet

import androidx.compose.foundation.Image
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.selection.toggleable
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import com.turnhub.android.data.PlaneInfo
import com.turnhub.android.standalone.PlanarSelection
import com.turnhub.android.standalone.StandaloneState
import com.turnhub.android.standalone.StandaloneTable
import com.turnhub.android.ui.components.AccentButton
import com.turnhub.android.ui.components.ToneButton
import com.turnhub.android.ui.theme.palette

/** Release filters and saved pools only change setup; the active deck is a snapshot. */
@Composable
internal fun PlanarDeckPicker(state: StandaloneState, table: StandaloneTable, cards: List<PlaneInfo>, enabled: Boolean) {
    val choice = state.game.planechase.selection
    var collectionsOpen by remember { mutableStateOf(false) }
    var browserOpen by rememberSaveable { mutableStateOf(false) }
    var problem by remember { mutableStateOf<String?>(null) }
    Text("Card selection", color = palette.muted, style = MaterialTheme.typography.titleSmall)
    Text(choice.name, color = palette.text, style = MaterialTheme.typography.titleMedium)
    if (!choice.available(cards.map { it.card })) Text("Some selected cards aren't downloaded. Refresh the library or choose another deck.", color = palette.bad)
    FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
        ToneButton("All planes", { if (!table.setPlanarSelection(PlanarSelection())) problem = "Couldn't change the deck." }, enabled = enabled)
        Box {
            ToneButton("Collection", { collectionsOpen = true }, enabled = enabled && cards.any { it.collections.isNotEmpty() })
            DropdownMenu(collectionsOpen, { collectionsOpen = false }, Modifier.heightIn(max = 360.dp)) {
                cards.flatMap { it.collections }.distinct().sorted().forEach { collection ->
                    DropdownMenuItem(text = { Text(collection) }, onClick = {
                        val ids = cards.filter { collection in it.collections }.map { it.id }.toSet()
                        if (!table.setPlanarSelection(PlanarSelection(collection, ids))) problem = "This collection needs at least one downloaded plane."
                        collectionsOpen = false
                    })
                }
            }
        }
        ToneButton("Browse & build", { browserOpen = true }, enabled = enabled)
    }
    if (cards.isNotEmpty() && cards.all { it.collections.isEmpty() })
        Text("Refresh the library to load collections, including Anthology.", color = palette.muted)
    state.library.planarDecks.forEach { deck ->
        FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
            ToneButton("Use ${deck.name}", {
                problem = if (table.setPlanarSelection(deck)) null else "Some cards in this deck aren't downloaded."
            }, enabled = enabled)
            ToneButton("Delete ${deck.name}", {
                problem = if (table.deletePlanarDeck(deck.name)) null else "Couldn't delete the deck."
            }, enabled = enabled)
        }
    }
    problem?.let { Text(it, color = palette.bad) }
    if (browserOpen) PlanarDeckBrowser(cards, choice, { selection -> table.savePlanarDeck(selection) }) { browserOpen = false }
}

@Composable
private fun PlanarDeckBrowser(cards: List<PlaneInfo>, choice: PlanarSelection, onSave: (PlanarSelection) -> Boolean, onClose: () -> Unit) {
    var selected by rememberSaveable { mutableStateOf<List<String>>((choice.cardIds ?: cards.map { it.id }).toList()) }
    var name by rememberSaveable { mutableStateOf(if (choice.cardIds == null) "My planar deck" else choice.name) }
    var query by rememberSaveable { mutableStateOf("") }
    var collection by rememberSaveable { mutableStateOf("") }
    var onlySelected by rememberSaveable { mutableStateOf(false) }
    var collectionMenu by remember { mutableStateOf(false) }
    var preview by remember { mutableStateOf<PlaneInfo?>(null) }
    var previewCards by remember { mutableStateOf<List<PlaneInfo>>(emptyList()) }
    var problem by remember { mutableStateOf<String?>(null) }
    val visible = remember(cards, query, collection, onlySelected, selected) {
        cards.filter { (query.isBlank() || it.name.contains(query, true) || it.typeLine.contains(query, true) || it.text.contains(query, true)) &&
            (collection.isEmpty() || collection in it.collections) && (!onlySelected || it.id in selected) }.sortedBy { it.name }
    }
    val planes = cards.count { it.id in selected && !it.phenomenon }
    val phenomena = cards.count { it.id in selected && it.phenomenon }
    fun toggle(card: PlaneInfo) {
        selected = if (card.id in selected) selected - card.id else selected + card.id
        problem = null
    }
    Dialog(onDismissRequest = onClose, properties = DialogProperties(usePlatformDefaultWidth = false)) {
        Surface(Modifier.fillMaxSize().systemBarsPadding(), color = palette.bg) {
            Column(Modifier.fillMaxSize().imePadding().padding(16.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                Text("Build a planar deck", color = palette.text, style = MaterialTheme.typography.titleLarge)
                LazyColumn(Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                    item {
                        Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                            OutlinedTextField(name, { name = it.take(64) }, label = { Text("Deck name") }, singleLine = true, modifier = Modifier.fillMaxWidth())
                            OutlinedTextField(query, { query = it }, label = { Text("Search cards") }, singleLine = true, modifier = Modifier.fillMaxWidth())
                            FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                                Box {
                                    ToneButton(if (collection.isEmpty()) "All collections" else collection, { collectionMenu = true })
                                    DropdownMenu(collectionMenu, { collectionMenu = false }, Modifier.heightIn(max = 300.dp)) {
                                        (listOf("") + cards.flatMap { it.collections }.distinct().sorted()).forEach { release ->
                                            DropdownMenuItem(text = { Text(release.ifEmpty { "All collections" }) }, onClick = { collection = release; collectionMenu = false })
                                        }
                                    }
                                }
                                FilterChip(onlySelected, { onlySelected = !onlySelected }, label = { Text("Selected only") })
                            }
                            Text("$planes planes · $phenomena phenomena", color = palette.muted)
                            FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                                ToneButton("Add shown", { selected = (selected + visible.map { it.id }).distinct() })
                                ToneButton("Remove shown", { selected = selected - visible.map { it.id }.toSet() })
                            }
                        }
                    }
                    if (visible.isEmpty()) item { Text("No cards match these filters.", color = palette.muted) }
                    items(visible, key = { it.id }) { card ->
                        Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
                            Row(Modifier.weight(1f).toggleable(card.id in selected, role = Role.Checkbox, onValueChange = { toggle(card) }).padding(vertical = 8.dp),
                                verticalAlignment = Alignment.CenterVertically) {
                                Checkbox(card.id in selected, onCheckedChange = null)
                                Column(Modifier.weight(1f)) {
                                    Text(card.name, color = palette.text)
                                    Text(card.typeLine, color = palette.muted, style = MaterialTheme.typography.bodySmall)
                                }
                            }
                            TextButton({ previewCards = visible; preview = card }) { Text("Preview") }
                        }
                    }
                }
                problem?.let { Text(it, color = palette.bad) }
                if (planes == 0) Text("Choose at least one plane.", color = palette.muted)
                Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp), verticalAlignment = Alignment.CenterVertically) {
                    AccentButton("Save & use deck", {
                        val selection = PlanarSelection(name.trim(), selected.toSet())
                        if (onSave(selection)) onClose() else problem = "Couldn't save. Check the cards and name; up to 20 decks can be saved."
                    }, modifier = Modifier.weight(1f), enabled = planes > 0 && name.isNotBlank())
                    ToneButton("Cancel", onClose)
                }
            }
        }
    }
    preview?.let { card ->
        // Keep the preview list stable while cards are added/removed or filters change.
        PlanarCardPreview(card, previewCards, selected.toSet(), ::toggle, { preview = it }) { preview = null }
    }
}

@Composable
private fun PlanarCardPreview(card: PlaneInfo, cards: List<PlaneInfo>, selected: Set<String>, onToggle: (PlaneInfo) -> Unit,
    onCard: (PlaneInfo) -> Unit, onClose: () -> Unit) {
    val context = LocalContext.current
    val art by produceState<androidx.compose.ui.graphics.ImageBitmap?>(null, card.id) { value = PlaneLibrary.art(context, card) }
    val index = cards.indexOfFirst { it.id == card.id }
    Dialog(onDismissRequest = onClose) {
        Surface(shape = MaterialTheme.shapes.large, color = palette.surface) {
            Column(Modifier.heightIn(max = 720.dp).padding(16.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
                Column(Modifier.weight(1f, fill = false).verticalScroll(rememberScrollState()), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                    Text(card.name, color = palette.text, style = MaterialTheme.typography.titleLarge)
                    art?.let { Image(it, contentDescription = null, contentScale = ContentScale.Fit, modifier = Modifier.fillMaxWidth().aspectRatio(4f / 3f)) }
                    Text(card.typeLine, color = palette.muted)
                    Text(card.text, color = palette.text)
                    Text("Art by ${card.artist}. Card from Scryfall.", color = palette.muted, style = MaterialTheme.typography.bodySmall)
                }
                FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    ToneButton("Previous", { cards.getOrNull(index - 1)?.let(onCard) }, enabled = index > 0)
                    ToneButton("Next", { cards.getOrNull(index + 1)?.let(onCard) }, enabled = index >= 0 && index < cards.lastIndex)
                    ToneButton(if (card.id in selected) "Remove card" else "Add card", { onToggle(card) })
                    ToneButton("Done", onClose)
                }
            }
        }
    }
}
