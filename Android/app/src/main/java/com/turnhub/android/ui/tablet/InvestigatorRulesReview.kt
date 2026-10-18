package com.turnhub.android.ui.tablet

import androidx.compose.foundation.selection.toggleable
import androidx.compose.ui.semantics.Role
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Checkbox
import androidx.compose.material3.FilterChip
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.key
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalUriHandler
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.turnhub.android.domain.InvestigatorCard
import com.turnhub.android.domain.InvestigatorCardCatalogue
import com.turnhub.android.domain.InvestigatorCardRules
import com.turnhub.android.domain.InvestigatorCardSlot
import com.turnhub.android.ui.components.ToneButton
import com.turnhub.android.ui.theme.palette

@Composable
internal fun InvestigatorRuleFields(card: InvestigatorCard, slot: InvestigatorCardSlot, onChange: (InvestigatorCard) -> Unit) {
    val relic = slot == InvestigatorCardSlot.RELIC
    val draft = InvestigatorCardRules.draft(card.catalogueId)
    val uri = LocalUriHandler.current
    Text("Compare with your physical card before checking. Verify timing, quantities, targets, symbols and exceptions.",
        style = MaterialTheme.typography.bodySmall, color = palette.muted)
    if (relic) Text("Include the printed flip requirement in the starting-face notes. Review each face separately.",
        style = MaterialTheme.typography.bodySmall, color = palette.muted)
    draft?.let {
        Text("Community draft · not an exact transcription. ${it.reviewNote}", style = MaterialTheme.typography.bodySmall, color = palette.muted)
        TextButton(onClick = { uri.openUri(it.source) }) { Text("Rules source") }
    }
    OutlinedTextField(card.rules, { onChange(card.copy(rules = it.take(InvestigatorCard.MAX_RULES))) },
        label = { Text(if (relic) "Starting face rules / flip requirement" else "Rules / notes") }, modifier = Modifier.fillMaxWidth(), minLines = 3)
    if (card.rules.isBlank()) Text("Rules missing — enter them from your card or provide a readable scan.", color = palette.muted)
    RuleCheck(if (relic) "Starting face checked against my card" else "Checked against my physical card", card.rulesVerified,
        card.rules.isNotBlank()) { onChange(card.verifyRules(it)) }
    if (relic) {
        OutlinedTextField(card.transformedRules, { onChange(card.copy(transformedRules = it.take(InvestigatorCard.MAX_RULES))) },
            label = { Text("Transformed face rules / notes") }, modifier = Modifier.fillMaxWidth(), minLines = 3)
        if (card.transformedRules.isBlank()) Text("Transformed rules missing — check the reverse of your relic.", color = palette.muted)
        RuleCheck("Transformed face checked against my card", card.transformedRulesVerified, card.transformedRules.isNotBlank()) {
            onChange(card.verifyTransformedRules(it))
        }
    }
}

@Composable
private fun RuleCheck(label: String, checked: Boolean, enabled: Boolean, onCheck: (Boolean) -> Unit) {
    Row(Modifier.fillMaxWidth().heightIn(min = 48.dp).toggleable(value = checked, enabled = enabled, role = Role.Checkbox, onValueChange = onCheck),
        verticalAlignment = Alignment.CenterVertically) {
        Checkbox(checked, null, enabled = enabled)
        Text(label, modifier = Modifier.weight(1f), style = MaterialTheme.typography.bodySmall)
    }
}

internal fun ruleReviewStatus(card: InvestigatorCard, slot: InvestigatorCardSlot): String = when {
    card.rules.isBlank() || (slot == InvestigatorCardSlot.RELIC && card.transformedRules.isBlank()) -> "Rules missing · needs card or scan"
    card.rulesVerified && (slot != InvestigatorCardSlot.RELIC || card.transformedRulesVerified) -> "Checked against physical card"
    else -> "Needs physical-card verification"
}

/** Device-wide review, available without seating an investigator or changing a match. */
@Composable
internal fun InvestigatorReferenceReviewButton(references: Map<String, InvestigatorCard>, onSave: (String, InvestigatorCard) -> Boolean) {
    var open by rememberSaveable { mutableStateOf(false) }
    var editing by rememberSaveable { mutableStateOf<String?>(null) }
    var slot by rememberSaveable { mutableStateOf(InvestigatorCardSlot.SANITY_CONDITION) }
    var query by rememberSaveable { mutableStateOf("") }
    var pendingOnly by rememberSaveable { mutableStateOf(true) }
    ToneButton("Review condition & relic rules", { open = true })
    if (open) AlertDialog(onDismissRequest = { open = false; editing = null }, title = { Text("Physical-card verification") }, text = {
        Column(Modifier.heightIn(max = 460.dp).verticalScroll(rememberScrollState()), verticalArrangement = Arrangement.spacedBy(8.dp)) {
            Text("Correct the reminders from your cards, then check each face. Saved references are used for future assignments; existing match notes stay unchanged.",
                style = MaterialTheme.typography.bodySmall)
            Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                InvestigatorCardSlot.entries.forEach { s -> FilterChip(slot == s, { slot = s }, label = { Text(s.label) }) }
            }
            OutlinedTextField(query, { query = it.take(80) }, label = { Text("Search card or set") }, singleLine = true)
            RuleCheck("Only cards needing review", pendingOnly, true) { pendingOnly = it }
            val all = InvestigatorCardCatalogue.search(slot, "")
            val done = all.count { entry -> val c = references[entry.id] ?: entry.card()
                c.rulesVerified && (slot != InvestigatorCardSlot.RELIC || c.transformedRulesVerified) }
            Text("$done / ${all.size} checked", fontWeight = FontWeight.SemiBold)
            val matches = InvestigatorCardCatalogue.search(slot, query).filter { entry ->
                val c = references[entry.id] ?: entry.card()
                !pendingOnly || !c.rulesVerified || (slot == InvestigatorCardSlot.RELIC && !c.transformedRulesVerified)
            }
            if (matches.isEmpty()) Text("No cards match this view.")
            matches.forEach { entry ->
                val c = references[entry.id] ?: entry.card()
                ToneButton(entry.name, { editing = entry.id }, modifier = Modifier.fillMaxWidth())
                Text(ruleReviewStatus(c, slot), style = MaterialTheme.typography.bodySmall, color = palette.muted)
            }
        }
    }, confirmButton = { TextButton({ open = false }) { Text("Close") } })
    if (open) editing?.let { id ->
        val entry = InvestigatorCardCatalogue.find(id) ?: return@let
        key(id) {
            var rules by rememberSaveable { mutableStateOf((references[id] ?: entry.card()).rules) }
            var transformed by rememberSaveable { mutableStateOf((references[id] ?: entry.card()).transformedRules) }
            var frontHash by rememberSaveable { mutableStateOf((references[id] ?: entry.card()).verifiedRulesHash) }
            var backHash by rememberSaveable { mutableStateOf((references[id] ?: entry.card()).verifiedTransformedRulesHash) }
            var failed by remember { mutableStateOf(false) }
            val card = InvestigatorCard(entry.name, rules, id, transformed, frontHash, backHash)
            AlertDialog(onDismissRequest = { editing = null }, title = { Text(entry.name) }, text = {
                Column(Modifier.heightIn(max = 440.dp).verticalScroll(rememberScrollState()), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                    InvestigatorCardReferenceButton(id)
                    InvestigatorRuleFields(card, entry.slot) { changed ->
                        rules = changed.rules; transformed = changed.transformedRules
                        frontHash = changed.verifiedRulesHash; backHash = changed.verifiedTransformedRulesHash
                    }
                    if (failed) Text("Could not save this reference. Your edits are still open.", color = palette.bad)
                }
            }, confirmButton = { TextButton({ if (onSave(id, card)) editing = null else failed = true }) { Text("Save reference") } },
                dismissButton = { TextButton({ editing = null }) { Text("Cancel") } })
        }
    }
}
