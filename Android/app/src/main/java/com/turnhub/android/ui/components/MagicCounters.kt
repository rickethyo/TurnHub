package com.turnhub.android.ui.components

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.ui.draw.clip
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
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

/** One counter as a chip: a symbol and value, with its name unless [compact]. Words always ride along for TalkBack. */
private data class CounterChip(val glyph: String, val value: String?, val label: String, val tone: Tone)

private fun counterChips(c: PlayerCounters, initiative: Boolean, commander: Boolean, poisonLimit: Int): List<CounterChip> = buildList {
    if (c.poison > 0) add(CounterChip("☠", "${c.poison}/$poisonLimit", "Poison", if (c.poison >= poisonLimit - 3) Tone.BAD else Tone.WARN))
    if (initiative) add(CounterChip("⚔", null, "Initiative", Tone.ACCENT))
    if (c.cityBlessing) add(CounterChip("✦", null, "City’s blessing", Tone.ACCENT))
    if (c.energy > 0) add(CounterChip("⚡", "${c.energy}", "Energy", Tone.INFO))
    if (c.experience > 0) add(CounterChip("★", "${c.experience}", "Experience", Tone.GOOD))
    if (commander) for (n in 1..2) {
        val tax = c.commanderTax(n)
        if (tax > 0) add(CounterChip("⌘", "+$tax", "Commander $n tax", Tone.NEUTRAL))
    }
}

/**
 * The counters a player carries, as colored chips on their card: only what is
 * in play, so an empty board stays quiet. [compact] drops the names (life tiles).
 */
@OptIn(ExperimentalLayoutApi::class)
@Composable
fun CounterChips(
    counters: PlayerCounters,
    initiative: Boolean,
    commander: Boolean,
    poisonLimit: Int,
    modifier: Modifier = Modifier,
    compact: Boolean = false,
) {
    val chips = counterChips(counters, initiative, commander, poisonLimit)
    if (chips.isEmpty()) return
    val p = palette
    FlowRow(
        modifier,
        horizontalArrangement = Arrangement.spacedBy(6.dp, if (compact) Alignment.CenterHorizontally else Alignment.Start),
        verticalArrangement = Arrangement.spacedBy(6.dp),
    ) {
        chips.forEach { chip ->
            val color = if (chip.tone == Tone.NEUTRAL) p.text else toneColor(chip.tone)
            val shape = RoundedCornerShape(10.dp)
            Row(
                Modifier
                    .clip(shape)
                    .background(color.copy(alpha = .12f))
                    .border(1.dp, color.copy(alpha = .35f), shape)
                    .padding(horizontal = if (compact) 7.dp else 10.dp, vertical = if (compact) 3.dp else 6.dp)
                    .semantics(mergeDescendants = true) {
                        contentDescription = chip.label + (chip.value?.let { " $it" } ?: "")
                    },
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(5.dp),
            ) {
                Text(chip.glyph, color = color, style = if (compact) MaterialTheme.typography.labelMedium else MaterialTheme.typography.titleMedium)
                chip.value?.let {
                    Text(it, color = color, style = (if (compact) MaterialTheme.typography.labelMedium else MaterialTheme.typography.titleMedium)
                        .copy(fontWeight = FontWeight.Bold))
                }
                if (!compact || chip.value == null) {
                    Text(chip.label, color = color, style = MaterialTheme.typography.labelMedium)
                }
            }
        }
    }
}

@Composable
fun MagicCountersEditor(counters: PlayerCounters, initiative: Boolean, commander: Boolean, enabled: Boolean = true,
    onChange: (kind: String, value: Int, commander: Int) -> Unit,
) {
    Column(verticalArrangement = Arrangement.spacedBy(6.dp)) {
        CounterRow("Poison", counters.poison, enabled, "☠", Tone.WARN) { onChange("poison", it, 1) }
        CounterRow("Energy", counters.energy, enabled, "⚡", Tone.INFO) { onChange("energy", it, 1) }
        CounterRow("Experience", counters.experience, enabled, "★", Tone.GOOD) { onChange("experience", it, 1) }
        ToneButton(if (initiative) "Release initiative" else "I have the initiative",
            { onChange("initiative", if (initiative) 0 else 1, 1) }, Modifier.fillMaxWidth(), enabled = enabled)
        ToneButton(if (counters.cityBlessing) "Clear city’s blessing (correction)" else "I have the city’s blessing",
            { onChange("city-blessing", if (counters.cityBlessing) 0 else 1, 1) }, Modifier.fillMaxWidth(), enabled = enabled)
        if (commander) {
            Text("Casts from the command zone", color = palette.muted, style = MaterialTheme.typography.labelLarge)
            for (c in 1..2) {
                CounterRow("Commander $c casts · next tax +${counters.commanderTax(c)}", counters.commanderCasts[c-1], enabled, "⌘") {
                    onChange("commander-casts", it, c)
                }
            }
            Text("Count only casts from the command zone. Each commander has its own tax; − corrects a cast.",
                color = palette.muted, style = MaterialTheme.typography.bodySmall)
        }
    }
}

@Composable
private fun CounterRow(label: String, value: Int, enabled: Boolean, glyph: String = "", tone: Tone = Tone.NEUTRAL, onDelta: (Int) -> Unit) {
    val p = palette
    val color = if (tone == Tone.NEUTRAL) p.text else toneColor(tone)
    Row(
        Modifier
            .fillMaxWidth()
            .clip(RoundedCornerShape(12.dp))
            .background(p.surface2)
            .padding(horizontal = 12.dp, vertical = 4.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(10.dp),
    ) {
        if (glyph.isNotEmpty()) Text(glyph, color = color, style = MaterialTheme.typography.titleMedium)
        Text(label, color = p.text, modifier = Modifier.weight(1f))
        StepButton("−", "Subtract one $label", enabled && value > 0) { onDelta(-1) }
        Text("$value", color = color, style = MaterialTheme.typography.titleLarge.copy(fontWeight = FontWeight.Bold),
            textAlign = TextAlign.Center, modifier = Modifier.widthIn(min = 36.dp))
        StepButton("+", "Add one $label", enabled && value < 1_000_000) { onDelta(1) }
    }
}

@Composable
private fun StepButton(text: String, description: String, enabled: Boolean, onClick: () -> Unit) {
    val p = palette
    Box(
        Modifier
            .size(40.dp)
            .clip(CircleShape)
            .background(if (enabled) p.fillStrong else p.surface2)
            .clickable(enabled = enabled, role = Role.Button, onClick = onClick)
            .semantics { contentDescription = description },
        contentAlignment = Alignment.Center,
    ) {
        Text(text, color = if (enabled) p.text else p.faint, style = MaterialTheme.typography.titleMedium.copy(fontWeight = FontWeight.Bold))
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
