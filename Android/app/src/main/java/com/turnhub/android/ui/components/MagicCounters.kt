package com.turnhub.android.ui.components

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.unit.dp
import com.turnhub.android.protocol.GameProfile
import com.turnhub.android.protocol.PlayerCounters
import com.turnhub.android.protocol.TableSettings
import com.turnhub.android.ui.theme.palette

fun magicStatus(c: PlayerCounters, initiative: Boolean): String = buildList {
    add("Poison ${c.poison}")
    if (initiative) add("Initiative")
    if (c.cityBlessing) add("City’s blessing")
    if (c.energy > 0) add("Energy ${c.energy}")
    if (c.experience > 0) add("Experience ${c.experience}")
}.joinToString(" · ")

@Composable
fun MagicCountersEditor(counters: PlayerCounters, initiative: Boolean, commander: Boolean, enabled: Boolean = true,
    onChange: (kind: String, value: Int, commander: Int) -> Unit,
) {
    Column(verticalArrangement = Arrangement.spacedBy(6.dp)) {
        CounterRow("Poison", counters.poison, enabled) { onChange("poison", it, 1) }
        CounterRow("Energy", counters.energy, enabled) { onChange("energy", it, 1) }
        CounterRow("Experience", counters.experience, enabled) { onChange("experience", it, 1) }
        ToneButton(if (initiative) "Release initiative" else "I have the initiative",
            { onChange("initiative", if (initiative) 0 else 1, 1) }, Modifier.fillMaxWidth(), enabled = enabled)
        ToneButton(if (counters.cityBlessing) "Clear city’s blessing (correction)" else "I have the city’s blessing",
            { onChange("city-blessing", if (counters.cityBlessing) 0 else 1, 1) }, Modifier.fillMaxWidth(), enabled = enabled)
        if (commander) {
            Text("Casts from the command zone", color = palette.muted, style = MaterialTheme.typography.labelLarge)
            for (c in 1..2) {
                CounterRow("Commander $c casts", counters.commanderCasts[c-1], enabled) { onChange("commander-casts", it, c) }
                Text("Commander $c: next cast tax +${counters.commanderTax(c)}", color = palette.text)
            }
            Text("Count only casts from the command zone. Each commander has its own tax; − corrects a cast.",
                color = palette.muted, style = MaterialTheme.typography.bodySmall)
        }
    }
}

@Composable
private fun CounterRow(label: String, value: Int, enabled: Boolean, onDelta: (Int) -> Unit) {
    Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
        Text("$label: $value", color = palette.text, modifier = Modifier.weight(1f))
        TextButton({ onDelta(-1) }, modifier = Modifier.semantics { contentDescription = "Subtract one $label" }, enabled = enabled && value > 0) { Text("−") }
        TextButton({ onDelta(1) }, modifier = Modifier.semantics { contentDescription = "Add one $label" }, enabled = enabled && value < 1_000_000) { Text("+") }
    }
}

@Composable
fun EliminationRulesEditor(settings: TableSettings, enabled: Boolean = true, onChange: (field: String, value: Int) -> Unit) {
    RuleToggle("Zero life", "autoEliminateAtZeroLife", settings.autoEliminateAtZeroLife, enabled, onChange)
    if (settings.profile == GameProfile.MTG || settings.profile == GameProfile.MTG_COMMANDER) {
        RuleToggle("Poison limit", "autoEliminateAtPoisonLimit", settings.autoEliminateAtPoisonLimit, enabled, onChange)
        var limit by remember(settings.poisonLimit) { mutableStateOf("${settings.poisonLimit}") }
        OutlinedTextField(limit, { limit = it }, label = { Text("Poison limit${if (settings.twoHeadedGiant) " (shared team total)" else ""}") },
            singleLine = true, enabled = enabled, keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number), modifier = Modifier.fillMaxWidth())
        TextButton({ limit.toIntOrNull()?.let { onChange("poisonLimit", it) } },
            enabled = enabled && limit.toIntOrNull()?.let { it in 1..1_000_000 && it != settings.poisonLimit } == true) { Text("Apply poison limit") }
        if (settings.profile == GameProfile.MTG_COMMANDER)
            RuleToggle("21 damage from one commander", "autoEliminateAtCommanderDamage", settings.autoEliminateAtCommanderDamage, enabled, onChange)
    }
    Text("Automatic elimination applies immediately when enabled or a limit changes. Disable the relevant rule before a card changes the loss condition.",
        color = palette.muted, style = MaterialTheme.typography.bodySmall)
}

@Composable
private fun RuleToggle(label: String, field: String, checked: Boolean, enabled: Boolean, onChange: (String, Int) -> Unit) {
    TextButton({ onChange(field, if (checked) 0 else 1) }, enabled = enabled) {
        Text("Automatic elimination — $label: ${if (checked) "On" else "Off"}")
    }
}
