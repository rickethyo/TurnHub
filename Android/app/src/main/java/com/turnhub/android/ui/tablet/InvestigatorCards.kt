package com.turnhub.android.ui.tablet

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.key
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.turnhub.android.domain.InvestigatorCard
import com.turnhub.android.domain.InvestigatorCardSlot
import com.turnhub.android.domain.TablePlayer
import com.turnhub.android.ui.components.ToneButton
import com.turnhub.android.ui.theme.palette

/** The same two assignment slots in setup and on the investigator's live card/menu. */
@OptIn(ExperimentalLayoutApi::class)
@Composable
internal fun InvestigatorCards(player: TablePlayer, gameId: String, enabled: Boolean, compact: Boolean = false,
    onAssign: (InvestigatorCardSlot, InvestigatorCard?) -> Unit) {
    val p = palette
    var editing by rememberSaveable(player.participantId, player.investigator, gameId) { mutableStateOf<InvestigatorCardSlot?>(null) }
    if (compact) FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
        InvestigatorCardSlot.entries.forEach { slot ->
            val card = player.dashboard.card(slot)
            ToneButton(card?.let { "${slot.label}: ${it.name}" } ?: "Assign ${slot.label.lowercase()}",
                { editing = slot }, enabled = enabled, modifier = Modifier.semantics {
                    contentDescription = "${player.label}, ${slot.label}: ${card?.name ?: "unassigned"}. Edit assignment"
                })
        }
    } else Column(Modifier.fillMaxWidth(), verticalArrangement = Arrangement.spacedBy(10.dp)) {
        InvestigatorCardSlot.entries.forEach { slot ->
            val card = player.dashboard.card(slot)
            Column(Modifier.fillMaxWidth().background(p.surface.copy(alpha = .94f), RoundedCornerShape(14.dp)).padding(12.dp),
                verticalArrangement = Arrangement.spacedBy(6.dp)) {
                Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    Text(slot.label, color = p.muted, style = MaterialTheme.typography.labelLarge,
                        modifier = Modifier.weight(1f).semantics { heading() })
                    ToneButton(if (card == null) "Assign" else "Edit", { editing = slot }, enabled = enabled,
                        modifier = Modifier.semantics { contentDescription = "Edit ${player.label}'s ${slot.label.lowercase()}" })
                }
                Text(card?.name ?: "Unassigned", color = p.text, fontWeight = FontWeight.SemiBold)
                card?.rules?.takeIf { it.isNotEmpty() }?.let { Text(it, color = p.text, style = MaterialTheme.typography.bodyMedium) }
            }
        }
    }
    editing?.let { slot ->
        key(player.participantId, player.investigator, gameId, slot) {
            InvestigatorCardEditor(player, slot, player.dashboard.card(slot), onDismiss = { editing = null }, onSave = {
                if (enabled) onAssign(slot, it)
                editing = null
            })
        }
    }
}

@Composable
private fun InvestigatorCardEditor(player: TablePlayer, slot: InvestigatorCardSlot, card: InvestigatorCard?,
    onDismiss: () -> Unit, onSave: (InvestigatorCard?) -> Unit) {
    var name by rememberSaveable { mutableStateOf(card?.name.orEmpty()) }
    var rules by rememberSaveable { mutableStateOf(card?.rules.orEmpty()) }
    AlertDialog(onDismissRequest = onDismiss, title = { Text("${player.investigator ?: player.label}: ${slot.label.lowercase()}") },
        text = {
            Column(Modifier.fillMaxWidth().heightIn(max = 400.dp).verticalScroll(rememberScrollState()),
                verticalArrangement = Arrangement.spacedBy(10.dp)) {
                OutlinedTextField(name, { name = it.take(InvestigatorCard.MAX_NAME) }, label = { Text("Name") },
                    modifier = Modifier.fillMaxWidth())
                OutlinedTextField(rules, { rules = it.take(InvestigatorCard.MAX_RULES) }, label = { Text("Rules / notes") },
                    modifier = Modifier.fillMaxWidth(), minLines = 4)
                if (card != null) TextButton(onClick = { onSave(null) }) { Text("Remove ${slot.label.lowercase()}") }
            }
        },
        confirmButton = { TextButton(enabled = name.isNotBlank(), onClick = { onSave(InvestigatorCard(name, rules)) }) { Text("Save") } },
        dismissButton = { TextButton(onClick = onDismiss) { Text("Cancel") } })
}
