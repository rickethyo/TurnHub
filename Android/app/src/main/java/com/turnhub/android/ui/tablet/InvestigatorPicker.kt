package com.turnhub.android.ui.tablet

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
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
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.turnhub.android.domain.DeathMayDie
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
    val search = query.trim()
    val matches = DeathMayDie.boxes.mapNotNull { box ->
        box.investigators.filter { search.isEmpty() || it.contains(search, ignoreCase = true) }
            .takeIf { it.isNotEmpty() }?.let { box.title to it }
    }
    val typedNew = search.isNotEmpty() && DeathMayDie.all.none { it.equals(search, ignoreCase = true) } &&
        taken.keys.none { it.equals(search, ignoreCase = true) }
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text("${player.label}'s investigator") },
        text = {
            Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                OutlinedTextField(query, { query = it.take(StandaloneGame.MAX_NAME) }, label = { Text("Search or type a name") },
                    singleLine = true, modifier = Modifier.fillMaxWidth())
                Column(Modifier.weight(1f, fill = false).verticalScroll(rememberScrollState()), verticalArrangement = Arrangement.spacedBy(2.dp)) {
                    if (typedNew) InvestigatorRow("Use “$search”", null, false) { onSave(search) }
                    if (matches.isEmpty() && !typedNew) Text("No investigator matches.", color = p.muted)
                    matches.forEach { (box, names) ->
                        Text(box, color = p.muted, style = MaterialTheme.typography.labelLarge,
                            modifier = Modifier.padding(top = 8.dp).semantics { heading() })
                        names.forEach { name ->
                            val holder = taken[name]
                            InvestigatorRow(name, holder?.let { "Taken by $it" }, name == player.investigator,
                                enabled = holder == null) { onSave(name) }
                        }
                    }
                }
            }
        },
        confirmButton = { TextButton(onDismiss) { Text("Cancel") } },
        dismissButton = { if (player.investigator != null) TextButton({ onSave(null) }) { Text("Clear") } },
    )
}

@Composable
private fun InvestigatorRow(name: String, note: String?, chosen: Boolean, enabled: Boolean = true, onPick: () -> Unit) {
    val p = palette
    Column(
        Modifier
            .fillMaxWidth()
            .clip(RoundedCornerShape(8.dp))
            .clickable(enabled = enabled, onClickLabel = "Play $name", role = Role.Button, onClick = onPick)
            .semantics { selected = chosen }
            .padding(horizontal = 8.dp, vertical = 8.dp),
    ) {
        Text(if (chosen) "✓ $name" else name, color = if (enabled) p.text else p.faint, style = MaterialTheme.typography.bodyLarge,
            fontWeight = if (chosen) FontWeight.SemiBold else FontWeight.Normal)
        note?.let { Text(it, color = p.faint, style = MaterialTheme.typography.labelSmall) }
    }
}
