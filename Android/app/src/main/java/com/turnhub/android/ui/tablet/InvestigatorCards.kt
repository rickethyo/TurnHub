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
import com.turnhub.android.domain.InvestigatorCardCatalogue
import com.turnhub.android.domain.TablePlayer
import com.turnhub.android.ui.components.ToneButton
import com.turnhub.android.ui.theme.palette

/** The same two assignment slots in setup and on the investigator's live card/menu. */
@OptIn(ExperimentalLayoutApi::class)
@Composable
internal fun InvestigatorCards(player: TablePlayer, gameId: String, enabled: Boolean, compact: Boolean = false, useRelics: Boolean = false,
    drawTwo: Boolean = false, onDeal: ((InvestigatorCardSlot) -> Unit)? = null,
    reservedCardIds: Set<String> = emptySet(),
    references: Map<String, InvestigatorCard> = emptyMap(),
    onAssign: (InvestigatorCardSlot, InvestigatorCard?) -> Unit) {
    val p = palette
    val slots = InvestigatorCardSlot.entries.filter { it != InvestigatorCardSlot.RELIC || useRelics }
    var editing by rememberSaveable(player.participantId, player.investigator, gameId) { mutableStateOf<InvestigatorCardSlot?>(null) }
    if (compact) FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
        slots.forEach { slot ->
            val card = player.dashboard.card(slot)
            ToneButton(card?.let { "${slot.label}: ${it.name}" } ?: "Assign ${slot.label.lowercase()}",
                { editing = slot }, enabled = enabled, modifier = Modifier.semantics {
                    contentDescription = "${player.label}, ${slot.label}: ${card?.name ?: "unassigned"}. Edit assignment"
                })
        }
    } else Column(Modifier.fillMaxWidth(), verticalArrangement = Arrangement.spacedBy(10.dp)) {
        slots.forEach { slot ->
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
                if (card != null) {
                    Text(if (slot == InvestigatorCardSlot.RELIC) "Starting face · " +
                        (if (card.rulesVerified) "checked" else "needs verification") else ruleReviewStatus(card, slot),
                        color = p.muted, style = MaterialTheme.typography.bodySmall)
                    if (slot == InvestigatorCardSlot.RELIC) {
                        Text("Transformed face · " + (if (card.transformedRulesVerified) "checked" else "needs verification"),
                            color = p.muted, style = MaterialTheme.typography.bodySmall)
                        Text(card.transformedRules.ifBlank { "Transformed rules missing — check your card." }, color = p.text)
                    }
                }
                InvestigatorCardReferenceButton(card?.catalogueId)
            }
        }
    }
    editing?.takeIf { it in slots }?.let { slot ->
        key(player.participantId, player.investigator, gameId, slot) {
            InvestigatorCardEditor(player, slot, player.dashboard.card(slot), drawTwo, reservedCardIds, references, onDeal?.let { deal -> { deal(slot) } }, onDismiss = { editing = null }, onSave = {
                if (enabled) onAssign(slot, it)
                editing = null
            })
        }
    }
}

@Composable
private fun InvestigatorCardEditor(player: TablePlayer, slot: InvestigatorCardSlot, card: InvestigatorCard?,
    drawTwo: Boolean, reservedCardIds: Set<String>, references: Map<String, InvestigatorCard>, onDeal: (() -> Unit)?, onDismiss: () -> Unit, onSave: (InvestigatorCard?) -> Unit) {
    var name by rememberSaveable { mutableStateOf(card?.name.orEmpty()) }
    var rules by rememberSaveable { mutableStateOf(card?.rules.orEmpty()) }
    var catalogueId by rememberSaveable { mutableStateOf(card?.catalogueId) }
    var transformed by rememberSaveable { mutableStateOf(card?.transformedRules.orEmpty()) }
    var frontHash by rememberSaveable { mutableStateOf(card?.verifiedRulesHash) }
    var backHash by rememberSaveable { mutableStateOf(card?.verifiedTransformedRulesHash) }
    fun choose(preset: com.turnhub.android.domain.InvestigatorCardPreset) {
        if (preset.id != catalogueId) {
            val draft = references[preset.id] ?: preset.card()
            rules = draft.rules; transformed = draft.transformedRules
            frontHash = draft.verifiedRulesHash; backHash = draft.verifiedTransformedRulesHash
        }
        name = preset.name; catalogueId = preset.id
    }
    var choosing by rememberSaveable { mutableStateOf(card == null && !drawTwo) }
    var physical by rememberSaveable { mutableStateOf(false) }
    var physicalNames by rememberSaveable { mutableStateOf(listOf("", "")) }
    val offers = player.dashboard.cardOffers[slot].orEmpty().mapNotNull(InvestigatorCardCatalogue::find)
    val canDeal = offers.isNotEmpty() || InvestigatorCardCatalogue.all.count { it.slot == slot && it.id !in reservedCardIds } >= 2
    AlertDialog(onDismissRequest = onDismiss, title = { Text("${player.investigator ?: player.label}: ${slot.label.lowercase()}") },
        text = {
            Column(Modifier.fillMaxWidth().heightIn(max = 400.dp).verticalScroll(rememberScrollState()),
                verticalArrangement = Arrangement.spacedBy(10.dp)) {
                if (drawTwo) {
                    Text("Draw two, pick one · house rule", style = MaterialTheme.typography.titleSmall)
                    Text("App pairs use the current catalogue. Physical draws use your own deck.")
                    if (card == null && name.isBlank()) Text("Choose one of your two draws, then Save.")
                    ToneButton(if (offers.isEmpty()) "Deal two in app" else "Show app-dealt pair", {
                        physical = false; onDeal?.invoke()
                    }, enabled = onDeal != null && canDeal)
                    if (!canDeal) Text("Fewer than two catalogue cards remain. Enter physical draws or use a custom entry.")
                    if (!physical && offers.isNotEmpty()) {
                        Text("This pair stays fixed for this match. Choose one, then Save.")
                        offers.forEach { preset ->
                            ToneButton(if (catalogueId == preset.id) "✓ ${preset.name}" else preset.name, {
                                choose(preset)
                            }, modifier = Modifier.fillMaxWidth())
                            InvestigatorCardReferenceButton(preset.id)
                        }
                    }
                    ToneButton("Enter two physical draws", { physical = true })
                    if (physical) {
                        (0..1).forEach { i ->
                            OutlinedTextField(physicalNames[i], { value ->
                                physicalNames = physicalNames.mapIndexed { j, old -> if (i == j) value.take(InvestigatorCard.MAX_NAME) else old }
                            }, label = { Text("Physical draw ${i + 1}") }, modifier = Modifier.fillMaxWidth())
                            TextButton(enabled = physicalNames.all { it.isNotBlank() }, onClick = {
                                val preset = InvestigatorCardCatalogue.all.firstOrNull {
                                    it.slot == slot && it.name.equals(physicalNames[i].trim(), ignoreCase = true)
                                }
                                if (preset != null) choose(preset) else {
                                    name = physicalNames[i].trim(); catalogueId = null; rules = ""; transformed = ""; frontHash = null; backHash = null
                                }
                            }) { Text("Choose draw ${i + 1}") }
                        }
                    }
                }
                ToneButton("Choose from catalogue", { choosing = true }, modifier = Modifier.fillMaxWidth())
                InvestigatorCardReferenceButton(catalogueId)
                OutlinedTextField(name, {
                    name = it.take(InvestigatorCard.MAX_NAME)
                    if (!InvestigatorCardCatalogue.find(catalogueId)?.name.equals(name.trim(), ignoreCase = true)) catalogueId = null
                }, label = { Text("Name") },
                    modifier = Modifier.fillMaxWidth())
                val current = InvestigatorCard(name, rules, catalogueId, transformed, frontHash, backHash)
                InvestigatorRuleFields(current, slot) { changed ->
                    rules = changed.rules; transformed = changed.transformedRules
                    frontHash = changed.verifiedRulesHash; backHash = changed.verifiedTransformedRulesHash
                }
                val savedReference = references[catalogueId] ?: InvestigatorCardCatalogue.find(catalogueId)?.card()
                if (savedReference != null && ((rules.isBlank() && savedReference.rules.isNotBlank()) ||
                    (slot == InvestigatorCardSlot.RELIC && transformed.isBlank() && savedReference.transformedRules.isNotBlank()))) {
                    ToneButton("Fill missing rules from reference", {
                        if (rules.isBlank()) { rules = savedReference.rules; frontHash = savedReference.verifiedRulesHash }
                        if (slot == InvestigatorCardSlot.RELIC && transformed.isBlank()) {
                            transformed = savedReference.transformedRules; backHash = savedReference.verifiedTransformedRulesHash
                        }
                    })
                }
                if (card != null) TextButton(onClick = { onSave(null) }) { Text("Remove ${slot.label.lowercase()}") }
            }
        },
        confirmButton = { TextButton(enabled = name.isNotBlank(), onClick = { onSave(InvestigatorCard(name, rules, catalogueId, transformed, frontHash, backHash)) }) { Text("Save") } },
        dismissButton = { TextButton(onClick = onDismiss) { Text("Cancel") } })
    if (choosing) InvestigatorCardPicker(slot, catalogueId, onDismiss = { choosing = false }, references = references, onPick = { preset ->
        if (preset != null) choose(preset) else catalogueId = null
        choosing = false
    })
}
