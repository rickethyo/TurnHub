package com.turnhub.android.ui.tablet

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.dp
import com.turnhub.android.domain.InvestigatorCardCatalogue
import com.turnhub.android.domain.InvestigatorCardPreset
import com.turnhub.android.domain.InvestigatorCardSlot
import com.turnhub.android.ui.theme.palette

/** Selection edits the editor's draft; only its Save assigns a card to the game. */
@Composable
internal fun InvestigatorCardPicker(slot: InvestigatorCardSlot, chosenId: String?, onDismiss: () -> Unit,
    references: Map<String, com.turnhub.android.domain.InvestigatorCard> = emptyMap(),
    onPick: (InvestigatorCardPreset?) -> Unit) {
    val p = palette
    var query by rememberSaveable(slot) { mutableStateOf("") }
    val matches = InvestigatorCardCatalogue.search(slot, query)
    AlertDialog(onDismissRequest = onDismiss, title = { Text("Choose ${slot.label.lowercase()}") }, text = {
        Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
            Text("Community catalogue. Some expansions are missing; use a custom entry for any other card.",
                style = MaterialTheme.typography.bodySmall, color = p.muted)
            OutlinedTextField(query, { query = it.take(80) }, label = { Text("Search card or set") },
                singleLine = true, modifier = Modifier.fillMaxWidth())
            Column(Modifier.weight(1f, fill = false).heightIn(max = 320.dp).verticalScroll(rememberScrollState()),
                verticalArrangement = Arrangement.spacedBy(2.dp)) {
                if (matches.isEmpty()) Text("No matches. You can enter a custom card.", color = p.muted)
                matches.forEach { entry ->
                    Column(Modifier.fillMaxWidth().heightIn(min = 56.dp)
                        .clickable(role = Role.Button, onClickLabel = "Choose ${entry.name}") { onPick(entry) }
                        .semantics { selected = entry.id == chosenId }.padding(8.dp)) {
                        Text(if (entry.id == chosenId) "✓ ${entry.name}" else entry.name, color = p.text)
                        Text(ruleReviewStatus(references[entry.id] ?: entry.card(), slot),
                            color = p.muted, style = MaterialTheme.typography.labelSmall)
                        Text(entry.collection, color = p.muted, style = MaterialTheme.typography.labelSmall)
                    }
                }
            }
        }
    }, confirmButton = { TextButton(onClick = onDismiss) { Text("Cancel") } },
        dismissButton = { TextButton(onClick = { onPick(null) }) { Text("Use custom entry") } })
}
