package com.turnhub.android.ui.tablet

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.liveRegion
import androidx.compose.ui.semantics.LiveRegionMode
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
import androidx.compose.material3.FilterChip
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.turnhub.android.domain.InvestigatorDashboard
import com.turnhub.android.domain.InvestigatorCard
import com.turnhub.android.domain.InvestigatorCardSlot
import com.turnhub.android.domain.InvestigatorSkills
import com.turnhub.android.domain.InvestigatorSkill
import com.turnhub.android.domain.TablePlayer
import com.turnhub.android.domain.TableSummary
import com.turnhub.android.protocol.TableState
import com.turnhub.android.ui.components.ToneButton
import com.turnhub.android.ui.theme.palette

/** Skill markers never apply effects automatically: companions and episode effects still use the physical game. */
@OptIn(ExperimentalLayoutApi::class)
@Composable
internal fun InvestigatorAbilities(
    player: TablePlayer,
    summary: TableSummary,
    onInsanity: (Int) -> Unit,
    onLevel: (Int, Int) -> Unit,
    onEdit: (List<InvestigatorSkill>) -> Unit,
    onCard: (InvestigatorCardSlot, InvestigatorCard?) -> Unit,
) {
    val p = palette
    val board = player.dashboard
    val skills = board.skills(player.investigator)
    val editable = !player.eliminated && summary.state in listOf(TableState.RUNNING, TableState.PAUSED)
    var editing by rememberSaveable(player.participantId, player.investigator) { mutableStateOf(false) }
    var previewUpgrades by rememberSaveable(player.participantId, player.investigator) { mutableStateOf(false) }
    var correcting by rememberSaveable(player.participantId, player.investigator) { mutableStateOf(false) }
    Column(Modifier.fillMaxWidth(), verticalArrangement = Arrangement.spacedBy(10.dp)) {
        FlowRow(horizontalArrangement = Arrangement.spacedBy(12.dp), verticalArrangement = Arrangement.spacedBy(8.dp),
            itemVerticalAlignment = Alignment.CenterVertically) {
            Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                StepButton("−", "${player.label}: correct insanity down one", editable && board.insanity > 0) { onInsanity(-1) }
                Column(horizontalAlignment = Alignment.CenterHorizontally) {
                    Text("Insanity ${board.insanity}/${InvestigatorDashboard.MAX_INSANITY}", color = p.text, fontWeight = FontWeight.SemiBold)
                    Text("Bonus dice +${board.bonusDice}", color = p.muted, style = MaterialTheme.typography.labelMedium)
                }
                StepButton("+", "${player.label}: lose one sanity", editable && board.insanity < InvestigatorDashboard.MAX_INSANITY) { onInsanity(1) }
            }
            ToneButton("Edit abilities", { editing = true }, enabled = editable)
            ToneButton(if (previewUpgrades) "Current abilities" else "Preview upgrades", { previewUpgrades = !previewUpgrades })
        }
        // A threshold is a moment the table must act on, so it is a bordered alert that names
        // the condition and its rules, not a line of small print (owner, 2026-10-19).
        when {
            board.insanity == InvestigatorDashboard.MAX_INSANITY ->
                SanityAlert("☠ Consumed by madness", "${player.investigator ?: player.label} goes insane. Confirm the death from their menu.", null, p.bad)
            board.pendingLevelUps > 0 -> ThresholdAlert(player.investigator ?: player.label, board, skills, editable, onLevel)
        }
        InvestigatorCards(player, summary.bootId, editable, references = summary.investigatorReferences, useRelics = summary.useRelics, onAssign = onCard)
        skills.forEachIndexed { slot, skill ->
            val level = board.skillLevels.getOrElse(slot) { 1 }
            Column(Modifier.fillMaxWidth().background(p.surface.copy(alpha = .94f), RoundedCornerShape(14.dp)).padding(12.dp),
                verticalArrangement = Arrangement.spacedBy(6.dp)) {
                Text("${skill.name} · Level $level", color = p.text, fontWeight = FontWeight.SemiBold)
                // Levels go up from the threshold alert; these chips only fix a mistake (owner, 2026-10-20).
                if (correcting) SkillLevelChips("${player.label}, ${skill.name}", level, editable) { onLevel(slot, it) }
                val effects = skill.levels.take(if (previewUpgrades) 4 else level)
                if (effects.all { it.isBlank() }) Text("Add this skill's effects from your dashboard with Edit abilities.",
                    color = p.muted, style = MaterialTheme.typography.bodySmall)
                else effects.forEachIndexed { i, effect ->
                    if (effect.isNotBlank()) Text("${i + 1}${if (i >= level) " (upgrade)" else ""}: $effect",
                        color = if (i >= level) p.muted else p.text, style = MaterialTheme.typography.bodyMedium)
                }
            }
        }
        if (skills.isNotEmpty()) TextButton({ correcting = !correcting }) {
            Text(if (correcting) "Done correcting skill levels" else "Correct skill levels")
        }
    }
    if (editing) InvestigatorSkillEditor(player, skills, onDismiss = { editing = false }, onSave = {
        if (editable) onEdit(it)
        editing = false
    })
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun InvestigatorSkillEditor(player: TablePlayer, skills: List<InvestigatorSkill>, onDismiss: () -> Unit,
    onSave: (List<InvestigatorSkill>) -> Unit) {
    var fields by rememberSaveable(player.participantId, player.investigator) {
        mutableStateOf(skills.flatMap { listOf(it.name) + it.levels })
    }
    var slot by rememberSaveable { mutableStateOf(0) }
    val count = fields.size / 5
    val names = (0 until count).map { fields[it * 5].trim() }
    val valid = names.none { it.isBlank() } && names.map { it.lowercase(java.util.Locale.ROOT) }.distinct().size == count
    AlertDialog(onDismissRequest = onDismiss, title = { Text("${player.investigator ?: player.label}: abilities") },
        text = {
            Column(Modifier.fillMaxWidth().heightIn(max = 440.dp).verticalScroll(rememberScrollState()),
                verticalArrangement = Arrangement.spacedBy(10.dp)) {
                Text("Enter the effects on your dashboard. Higher levels add to earlier effects unless they say instead.")
                FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    (0 until count).forEach { i -> FilterChip(selected = slot == i, onClick = { slot = i }, label = { Text("Skill ${i + 1}") }) }
                }
                Text("Add skills gained from relics, companions or other effects. New skills begin at level 1; set their marker after saving.")
                if (count < InvestigatorDashboard.MAX_SKILLS) {
                    FlowRow(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                        InvestigatorSkills.presets.filter { preset -> names.none { it.equals(preset.name, ignoreCase = true) } }.forEach { preset ->
                            TextButton(onClick = { fields = fields + listOf(preset.name) + preset.levels; slot = count }) { Text("+ ${preset.name}") }
                        }
                        TextButton(onClick = { fields = fields + List(5) { "" }; slot = count }) { Text("+ Custom ability") }
                    }
                }
                if (slot >= 3) TextButton(onClick = {
                    fields = fields.filterIndexed { i, _ -> i / 5 != slot }; slot = 0
                }) { Text("Remove added ability") }
                fun change(index: Int, text: String, limit: Int) {
                    fields = fields.mapIndexed { i, old -> if (i == index) text.take(limit) else old }
                }
                OutlinedTextField(fields[slot * 5], { change(slot * 5, it, InvestigatorDashboard.MAX_SKILL_NAME) },
                    label = { Text("Skill name") }, modifier = Modifier.fillMaxWidth())
                (1..4).forEach { level ->
                    OutlinedTextField(fields[slot * 5 + level], { change(slot * 5 + level, it, InvestigatorDashboard.MAX_LEVEL_TEXT) },
                        label = { Text("Level $level effect") }, modifier = Modifier.fillMaxWidth(), minLines = 2)
                }
            }
        },
        confirmButton = { TextButton(enabled = valid, onClick = {
            onSave(List(count) { i -> InvestigatorSkill(fields[i * 5].trim(), fields.subList(i * 5 + 1, i * 5 + 5).toList()) })
        }) { Text("Save") } },
        dismissButton = { TextButton(onClick = onDismiss) { Text("Cancel") } })
}

/** Level chips 1 to 4, for correcting a skill marker by hand. */
@OptIn(ExperimentalLayoutApi::class)
@Composable
internal fun SkillLevelChips(label: String, level: Int, enabled: Boolean, onLevel: (Int) -> Unit) {
    FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
        (1..4).forEach { choice ->
            FilterChip(selected = level == choice, onClick = { onLevel(choice) }, enabled = enabled,
                label = { Text("$choice", fontWeight = FontWeight.Bold) }, modifier = Modifier.heightIn(min = 48.dp).semantics {
                    contentDescription = "$label, level $choice"
                })
        }
    }
}

/**
 * An Insanity Threshold to resolve (rulebook p. 8): the insanity card activates, then one skill
 * levels up. The alert names the card and its rules, and each skill that can still rise is a
 * button; choosing one is the Resolve step, and the alert leaves once no level-up is owed.
 */
@OptIn(ExperimentalLayoutApi::class)
@Composable
internal fun ThresholdAlert(name: String, board: InvestigatorDashboard, skills: List<InvestigatorSkill>, enabled: Boolean,
    onLevel: (Int, Int) -> Unit) {
    val p = palette
    val condition = board.sanityCondition
    Column(Modifier.fillMaxWidth().background(p.surface, RoundedCornerShape(14.dp))
        .border(3.dp, p.warn, RoundedCornerShape(14.dp)).padding(14.dp),
        verticalArrangement = Arrangement.spacedBy(8.dp)) {
        Text("⚠ Insanity threshold: $name", color = p.text, style = MaterialTheme.typography.titleLarge, fontWeight = FontWeight.Black,
            modifier = Modifier.semantics { heading(); liveRegion = LiveRegionMode.Assertive })
        Text("1. Resolve ${condition?.name ?: "your sanity condition"}.", color = p.text, style = MaterialTheme.typography.bodyLarge,
            fontWeight = FontWeight.SemiBold)
        condition?.rules?.takeIf { it.isNotBlank() }?.let { Text(it, color = p.text, style = MaterialTheme.typography.bodyMedium) }
        Text(if (board.pendingLevelUps > 1) "2. Level up ${board.pendingLevelUps} skills, one at a time." else "2. Level up one skill:",
            color = p.text, style = MaterialTheme.typography.bodyLarge, fontWeight = FontWeight.SemiBold)
        FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
            skills.forEachIndexed { slot, skill ->
                val level = board.skillLevels.getOrElse(slot) { 1 }
                if (level < 4) ToneButton("Resolve: ${skill.name} to level ${level + 1}", { onLevel(slot, level + 1) },
                    enabled = enabled, tone = com.turnhub.android.ui.components.Tone.WARN)
            }
        }
        if (skills.indices.all { board.skillLevels.getOrElse(it) { 1 } >= 4 }) Text("Every skill is at level 4, so nothing levels up.",
            color = p.muted, style = MaterialTheme.typography.bodyMedium)
    }
}

/** A high-contrast alert on the investigator's panel: a heading, what to do, and the card's rules. Words carry it, not color. */
@Composable
private fun SanityAlert(title: String, action: String, rules: String?, edge: androidx.compose.ui.graphics.Color) {
    val p = palette
    Column(Modifier.fillMaxWidth().background(p.surface, RoundedCornerShape(14.dp))
        .border(3.dp, edge, RoundedCornerShape(14.dp)).padding(14.dp),
        verticalArrangement = Arrangement.spacedBy(6.dp)) {
        Text(title, color = p.text, style = MaterialTheme.typography.titleLarge, fontWeight = FontWeight.Black,
            modifier = Modifier.semantics { heading(); liveRegion = LiveRegionMode.Assertive })
        Text(action, color = p.text, style = MaterialTheme.typography.bodyLarge, fontWeight = FontWeight.SemiBold)
        rules?.let { Text(it, color = p.text, style = MaterialTheme.typography.bodyMedium) }
    }
}