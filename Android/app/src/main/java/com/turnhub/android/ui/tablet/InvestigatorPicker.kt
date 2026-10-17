package com.turnhub.android.ui.tablet

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.heightIn
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
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalUriHandler
import androidx.compose.ui.draw.clip
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.turnhub.android.domain.DeathMayDie
import com.turnhub.android.domain.InvestigatorIndex
import com.turnhub.android.domain.InvestigatorSkills
import com.turnhub.android.ui.components.ToneButton
import com.turnhub.android.domain.TablePlayer
import com.turnhub.android.standalone.StandaloneGame
import com.turnhub.android.ui.theme.palette

/**
 * Death May Die: picks the investigator [player] plays, from every season and
 * box ([DeathMayDie.boxes]), or a name typed in. [taken] maps the investigators
 * other players already have to their names; each investigator has one figure.
 */
@Composable
internal fun InvestigatorPickerDialog(player: TablePlayer, taken: Map<String, String>, onSave: (String?) -> Unit, onDismiss: () -> Unit) {
    val p = palette
    var query by rememberSaveable { mutableStateOf("") }
    var skill by rememberSaveable { mutableStateOf<String?>(null) }
    var filterOpen by rememberSaveable { mutableStateOf(false) }
    var preview by rememberSaveable { mutableStateOf<String?>(null) }
    val search = query.trim()
    val matches = DeathMayDie.boxes.mapNotNull { box ->
        box.investigators.filter { InvestigatorIndex.matches(it, search, skill) }
            .takeIf { it.isNotEmpty() }?.let { box.title to it }
    }
    val typedNew = skill == null && search.isNotEmpty() && DeathMayDie.all.none { it.equals(search, ignoreCase = true) } &&
        taken.keys.none { it.equals(search, ignoreCase = true) }
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text("${player.label}'s investigator") },
        text = {
            Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                OutlinedTextField(query, { query = it.take(StandaloneGame.MAX_NAME) }, label = { Text("Search or type a name") },
                    singleLine = true, modifier = Modifier.fillMaxWidth())
                ToneButton("Ability: ${skill ?: "All investigators"}", { filterOpen = true }, modifier = Modifier.fillMaxWidth())
                val available = matches.flatMap { it.second }.any { name -> taken.keys.none { it.equals(name, ignoreCase = true) } }
                ToneButton("Random investigator", {
                    preview = InvestigatorIndex.random(query, skill, taken.keys)
                }, enabled = available, modifier = Modifier.fillMaxWidth())
                Text("Random uses this search and ability filter; taken investigators are excluded.", color = p.muted,
                    style = MaterialTheme.typography.bodySmall)
                if (skill != null) Text("Community skill index; unindexed investigators are excluded. Check your dashboard.",
                    color = p.muted, style = MaterialTheme.typography.bodySmall)
                Column(Modifier.weight(1f, fill = false).verticalScroll(rememberScrollState()), verticalArrangement = Arrangement.spacedBy(2.dp)) {
                    if (typedNew) InvestigatorRow("Preview “$search”", null, false) { preview = search }
                    if (matches.isEmpty() && !typedNew) Text("No investigator matches.", color = p.muted)
                    matches.forEach { (box, names) ->
                        Text(box, color = p.muted, style = MaterialTheme.typography.labelLarge,
                            modifier = Modifier.padding(top = 8.dp).semantics { heading() })
                        names.forEach { name ->
                            val holder = taken.entries.firstOrNull { it.key.equals(name, ignoreCase = true) }?.value
                            InvestigatorRow(name, holder?.let { "Taken by $it" } ?: InvestigatorIndex.skills(name)?.joinToString(" · "), name == player.investigator,
                                enabled = holder == null) { preview = name }
                        }
                    }
                }
            }
        },
        confirmButton = { TextButton(onDismiss) { Text("Cancel") } },
        dismissButton = { if (player.investigator != null) TextButton({ onSave(null) }) { Text("Clear") } },
    )
    if (filterOpen) AlertDialog(onDismissRequest = { filterOpen = false }, title = { Text("Filter by ability") },
        text = { Column(Modifier.verticalScroll(rememberScrollState())) {
            (listOf<String?>(null) + InvestigatorIndex.filters).forEach { option ->
                TextButton(onClick = { skill = option; filterOpen = false }, modifier = Modifier.fillMaxWidth()) {
                    Text((if (skill == option) "✓ " else "") + (option ?: "All investigators"))
                }
            }
        } }, confirmButton = { TextButton({ filterOpen = false }) { Text("Close") } })
    preview?.let { name ->
        InvestigatorPreview(name, onDismiss = { preview = null }, onChoose = { onSave(name) })
    }
}

@Composable
private fun InvestigatorPreview(name: String, onDismiss: () -> Unit, onChoose: () -> Unit) {
    val uri = LocalUriHandler.current
    AlertDialog(onDismissRequest = onDismiss, title = { Text(name) }, text = {
        Column(Modifier.fillMaxWidth().heightIn(max = 440.dp).verticalScroll(rememberScrollState()),
            verticalArrangement = Arrangement.spacedBy(10.dp)) {
            DeathMayDie.boxOf(name)?.let { Text(it, color = palette.muted) }
            if (!InvestigatorSkills.hasReference(name)) Text("Signature effects are not indexed. Check your dashboard; you can enter missing abilities during play.",
                color = palette.muted)
            InvestigatorSkills.forInvestigator(name).forEach { ability ->
                Text(ability.name, fontWeight = FontWeight.SemiBold)
                ability.levels.forEachIndexed { level, effect ->
                    if (effect.isNotBlank()) Text("${level + 1}: $effect", style = MaterialTheme.typography.bodySmall)
                }
            }
            TextButton(onClick = { uri.openUri(InvestigatorIndex.SOURCE) }) { Text("Community investigator guide") }
        }
    }, confirmButton = { TextButton(onChoose) { Text("Use investigator") } },
        dismissButton = { TextButton(onDismiss) { Text("Back") } })
}

@Composable
private fun InvestigatorRow(name: String, note: String?, chosen: Boolean, enabled: Boolean = true, onPick: () -> Unit) {
    val p = palette
    Column(
        Modifier
            .fillMaxWidth()
            .clip(RoundedCornerShape(8.dp))
            .clickable(enabled = enabled, onClickLabel = "Preview $name", role = Role.Button, onClick = onPick)
            .semantics { selected = chosen }
            .padding(horizontal = 8.dp, vertical = 8.dp),
    ) {
        Text(if (chosen) "✓ $name" else name, color = if (enabled) p.text else p.faint, style = MaterialTheme.typography.bodyLarge,
            fontWeight = if (chosen) FontWeight.SemiBold else FontWeight.Normal)
        note?.let { Text(it, color = p.faint, style = MaterialTheme.typography.labelSmall) }
    }
}
