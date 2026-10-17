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
) {
    val p = palette
    val board = player.dashboard
    val skills = board.skills(player.investigator)
    val editable = !player.eliminated && summary.state in listOf(TableState.RUNNING, TableState.PAUSED)
    var editing by rememberSaveable(player.participantId, player.investigator) { mutableStateOf(false) }
    var previewUpgrades by rememberSaveable(player.participantId, player.investigator) { mutableStateOf(false) }
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
        when {
            board.insanity == InvestigatorDashboard.MAX_INSANITY -> Text("Consumed by madness. Resolve the investigator's death from their menu.", color = p.bad)
            board.atThreshold -> Text("Insanity threshold: resolve your insanity card and advance one skill. Apply its effects on the board.", color = p.text,
                style = MaterialTheme.typography.bodySmall)
        }
        skills.forEachIndexed { slot, skill ->
            val level = board.skillLevels[slot]
            Column(Modifier.fillMaxWidth().background(p.surface.copy(alpha = .94f), RoundedCornerShape(14.dp)).padding(12.dp),
                verticalArrangement = Arrangement.spacedBy(6.dp)) {
                Text("${skill.name} · Level $level", color = p.text, fontWeight = FontWeight.SemiBold)
                FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
                    (1..4).forEach { choice ->
                        FilterChip(selected = level == choice, onClick = { onLevel(slot, choice) }, enabled = editable,
                            label = { Text("$choice") }, modifier = Modifier.heightIn(min = 48.dp).semantics {
                                contentDescription = "${player.label}, ${skill.name}, level $choice"
                            })
                    }
                }
                val effects = skill.levels.take(if (previewUpgrades) 4 else level)
                if (effects.all { it.isBlank() }) Text("Add this skill's effects from your dashboard with Edit abilities.",
                    color = p.muted, style = MaterialTheme.typography.bodySmall)
                else effects.forEachIndexed { i, effect ->
                    if (effect.isNotBlank()) Text("${i + 1}${if (i >= level) " (upgrade)" else ""}: $effect",
                        color = if (i >= level) p.muted else p.text, style = MaterialTheme.typography.bodyMedium)
                }
            }
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
    val valid = (0..2).all { fields[it * 5].isNotBlank() }
    AlertDialog(onDismissRequest = onDismiss, title = { Text("${player.investigator ?: player.label}: abilities") },
        text = {
            Column(Modifier.fillMaxWidth().heightIn(max = 440.dp).verticalScroll(rememberScrollState()),
                verticalArrangement = Arrangement.spacedBy(10.dp)) {
                Text("Enter the effects on your dashboard. Higher levels add to earlier effects unless they say instead.")
                FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    (0..2).forEach { i -> FilterChip(selected = slot == i, onClick = { slot = i }, label = { Text("Skill ${i + 1}") }) }
                }
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
            onSave(List(3) { i -> InvestigatorSkill(fields[i * 5].trim(), fields.subList(i * 5 + 1, i * 5 + 5).toList()) })
        }) { Text("Save") } },
        dismissButton = { TextButton(onClick = onDismiss) { Text("Cancel") } })
}
