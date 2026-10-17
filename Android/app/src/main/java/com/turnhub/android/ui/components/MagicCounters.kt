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
import androidx.compose.material3.Icon
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.ui.res.painterResource
import com.turnhub.android.ui.theme.DesignTokens
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.Switch
import androidx.compose.foundation.selection.toggleable
import androidx.compose.foundation.layout.heightIn
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
internal data class CounterChip(val glyph: String, val value: String?, val label: String, val tone: Tone, val icon: Int? = null)

internal fun counterChips(c: PlayerCounters, initiative: Boolean, commander: Boolean, poisonLimit: Int): List<CounterChip> = buildList {
    if (c.poison > 0) add(CounterChip("☠", "${c.poison}/$poisonLimit", "Poison", if (c.poison >= poisonLimit - 3) Tone.BAD else Tone.WARN))
    if (initiative) add(CounterChip("⚔", null, "Initiative", Tone.ACCENT))
    if (c.cityBlessing) add(CounterChip("✦", null, "City’s blessing", Tone.ACCENT))
    if (c.energy > 0) add(CounterChip("⚡", "${c.energy}", "Energy", Tone.INFO))
    if (c.experience > 0) add(CounterChip("★", "${c.experience}", "Experience", Tone.GOOD))
    if (commander) for (n in 1..2) {
        val tax = c.commanderTax(n)
        if (tax > 0) add(CounterChip("", "+$tax", "Commander $n tax", Tone.NEUTRAL, DesignTokens.Icons.commander))
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
    commanderRarity: String? = null,
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
                        contentDescription = chip.label + (chip.value?.let { " $it" } ?: "") + if (chip.icon != null && commanderRarity != null && chip.label == "Commander 1 tax") ", $commanderRarity" else ""
                    },
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(5.dp),
            ) {
                if (chip.icon != null) Icon(painterResource(chip.icon), contentDescription = null, tint = commanderIconColor(commanderRarity.takeIf { chip.label == "Commander 1 tax" }), modifier = Modifier.size(if (compact) 16.dp else 22.dp))
                else Text(chip.glyph, color = color, style = if (compact) MaterialTheme.typography.labelMedium else MaterialTheme.typography.titleMedium)
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
    /** False for a Two-Headed Giant player: the team's shared poison shows once, in [TeamPoisonRow]. */
    showPoison: Boolean = true,
    /** The commander the player named, used in place of "Commander 1". */
    commanderName: String? = null,
    commanderRarity: String? = null,
    partnerEnabled: Boolean? = null,
    onPartnerChange: ((Boolean) -> Unit)? = null,
    onChange: (kind: String, value: Int, commander: Int) -> Unit,
) {
    var localPartner by rememberSaveable(commanderName) { mutableStateOf(false) }
    val partner = partnerEnabled ?: localPartner
    Column(verticalArrangement = Arrangement.spacedBy(6.dp)) {
        if (showPoison) CounterRow("Poison", counters.poison, enabled, "☠", Tone.WARN) { onChange("poison", it, 1) }
        CounterRow("Energy", counters.energy, enabled, "⚡", Tone.INFO) { onChange("energy", it, 1) }
        CounterRow("Experience", counters.experience, enabled, "★", Tone.GOOD) { onChange("experience", it, 1) }
        ToneButton(if (initiative) "Release initiative" else "I have the initiative",
            { onChange("initiative", if (initiative) 0 else 1, 1) }, Modifier.fillMaxWidth(), enabled = enabled)
        ToneButton(if (counters.cityBlessing) "Clear city’s blessing (correction)" else "I have the city’s blessing",
            { onChange("city-blessing", if (counters.cityBlessing) 0 else 1, 1) }, Modifier.fillMaxWidth(), enabled = enabled)
        if (commander) {
            Row(Modifier.fillMaxWidth().heightIn(min = 48.dp)
                .toggleable(partner, enabled = enabled, role = Role.Switch, onValueChange = {
                    if (onPartnerChange != null) onPartnerChange(it) else localPartner = it
                }), verticalAlignment = Alignment.CenterVertically) {
                Text("Partner commanders", color = palette.text, modifier = Modifier.weight(1f))
                Switch(partner, onCheckedChange = null, enabled = enabled)
            }
            Text("Casts from the command zone", color = palette.muted, style = MaterialTheme.typography.labelLarge)
            for (c in 1..if (partner) 2 else 1) {
                val which = if (c == 1 && commanderName != null) commanderName else "Commander $c"
                CounterRow("$which casts · next tax +${counters.commanderTax(c)}", counters.commanderCasts[c-1], enabled, icon = DesignTokens.Icons.commander, iconRarity = commanderRarity.takeIf { c == 1 }) {
                    onChange("commander-casts", it, c)
                }
            }
            Text("Count only casts from the command zone. Each commander has its own tax; − corrects a cast.",
                color = palette.muted, style = MaterialTheme.typography.bodySmall)
        }
    }
}

/** A Two-Headed Giant team's poison: shared by both players (CR 810.10), so shown once. */
@Composable
fun TeamPoisonRow(poison: Int, enabled: Boolean, onChange: (Int) -> Unit) =
    CounterRow("Team poison", poison, enabled, "☠", Tone.WARN, onDelta = onChange)

@Composable
private fun CounterRow(label: String, value: Int, enabled: Boolean, glyph: String = "", tone: Tone = Tone.NEUTRAL, icon: Int? = null, iconRarity: String? = null, onDelta: (Int) -> Unit) {
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
        if (icon != null) Icon(painterResource(icon), contentDescription = iconRarity?.let { "$it rarity" }, tint = commanderIconColor(iconRarity), modifier = Modifier.size(22.dp))
        else if (glyph.isNotEmpty()) Text(glyph, color = color, style = MaterialTheme.typography.titleMedium)
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
fun EliminationRulesEditor(settings: TableSettings, enabled: Boolean = true,
    /** An official mode without house rules: the poison limit is the official one. */
    limitsLocked: Boolean = false,
    onChange: (field: String, value: Int) -> Unit) {
    // Pokémon and Lorcana have no life total to lose: prizes and lore end the game.
    if (!settings.profile.countsLife) {
        Text(when {
            settings.profile.pokemon -> "Taking your last Prize card wins the game. Other ways to win are in each player's menu."
            settings.profile.deathMayDie -> "The investigators win or lose together, from any player's menu. Nobody is out automatically."
            else -> "Reaching ${settings.startingLife} lore wins the game. A player who can't draw is out from their menu."
        },
            color = palette.muted, style = MaterialTheme.typography.bodySmall)
        return
    }
    RuleToggle("Zero life", "autoEliminateAtZeroLife", settings.autoEliminateAtZeroLife, enabled, onChange)
    if (settings.profile.magicRules && limitsLocked) {
        RuleToggle("Poison limit", "autoEliminateAtPoisonLimit", settings.autoEliminateAtPoisonLimit, enabled, onChange)
        Text("Official poison limit: ${settings.poisonLimit}${if (settings.twoHeadedGiant) ", shared by the team" else ""}.",
            color = palette.muted, style = MaterialTheme.typography.bodySmall)
        if (settings.profile.commanderRules)
            RuleToggle("21 damage from one commander", "autoEliminateAtCommanderDamage", settings.autoEliminateAtCommanderDamage, enabled, onChange)
    } else if (settings.profile.magicRules) {
        RuleToggle("Poison limit", "autoEliminateAtPoisonLimit", settings.autoEliminateAtPoisonLimit, enabled, onChange)
        var limit by remember(settings.poisonLimit) { mutableStateOf("${settings.poisonLimit}") }
        OutlinedTextField(limit, { limit = it }, label = { Text("Poison limit${if (settings.twoHeadedGiant) " (shared team total)" else ""}") },
            singleLine = true, enabled = enabled, keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number), modifier = Modifier.fillMaxWidth())
        ToneButton("Apply poison limit", { limit.toIntOrNull()?.let { onChange("poisonLimit", it) } }, Modifier.fillMaxWidth(),
            enabled = enabled && limit.toIntOrNull()?.let { it in 1..1_000_000 && it != settings.poisonLimit } == true)
        if (settings.profile.commanderRules)
            RuleToggle("21 damage from one commander", "autoEliminateAtCommanderDamage", settings.autoEliminateAtCommanderDamage, enabled, onChange)
    }
    Text("Changes apply immediately. Turn a rule off when a card prevents that loss condition.",
        color = palette.muted, style = MaterialTheme.typography.bodySmall)
}

@Composable
private fun RuleToggle(label: String, field: String, checked: Boolean, enabled: Boolean, onChange: (String, Int) -> Unit) {
    Row(Modifier.fillMaxWidth().heightIn(min = 56.dp)
        .toggleable(checked, enabled = enabled, role = Role.Switch, onValueChange = { onChange(field, if (it) 1 else 0) })
        .padding(horizontal = 4.dp), verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(12.dp)) {
        Text("Eliminate at $label", color = palette.text, modifier = Modifier.weight(1f))
        Switch(checked, onCheckedChange = null, enabled = enabled)
    }
}

/** Rarity colours apply only to original artwork; unknown values use normal ink. */
@Composable
private fun commanderIconColor(rarity: String?) = when (rarity) {
    "common" -> palette.rarityCommon
    "uncommon" -> palette.rarityUncommon
    "rare" -> palette.rarityRare
    "mythic" -> palette.rarityMythic
    else -> palette.text
}
