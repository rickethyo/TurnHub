package com.turnhub.android.ui.tablet

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.FilterChip
import androidx.compose.material3.FilterChipDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import com.turnhub.android.domain.SpecialCondition
import com.turnhub.android.domain.TablePlayer
import com.turnhub.android.domain.TableSummary
import com.turnhub.android.domain.TurnAction
import com.turnhub.android.protocol.GameProfile
import com.turnhub.android.protocol.TableState
import com.turnhub.android.standalone.StandaloneGame
import com.turnhub.android.ui.components.Tone
import com.turnhub.android.ui.theme.palette

// Pokémon and Lorcana on the full-screen table (phone-hosted games): the panel's
// number is prizes left or lore; these are the words and reminders around it.
// Everything is text, never color alone.

/** The words under a panel's number; null for a life total, which needs none. */
internal fun scoreCaption(profile: GameProfile, score: Int, goal: Int): String? = when {
    profile.pokemon -> if (score == 1) "prize left" else "prizes left"
    profile.lorcana -> "lore · $goal to win"
    else -> null
}

/** What TalkBack reads for a panel's number. */
internal fun scoreDescription(profile: GameProfile, name: String, score: Int, goal: Int): String = when {
    profile.pokemon -> "$name, $score Prize cards left"
    profile.lorcana -> "$name, $score lore of $goal"
    else -> "$name, life $score"
}

/** The panel's minus and plus halves, for TalkBack. */
internal fun stepLabel(profile: GameProfile, name: String, step: Int, adding: Boolean): String = when {
    profile.pokemon -> if (adding) "$name: put a Prize card back" else "$name: take a Prize card"
    profile.lorcana -> if (adding) "$name: gain $step lore" else "$name: lose $step lore"
    else -> if (adding) "$name: add $step life" else "$name: subtract $step life"
}

/** The player whose once-per-turn actions this panel shows: its member holding the turn. */
private fun turnHolder(unit: TableUnit, summary: TableSummary): TablePlayer? =
    unit.members.firstOrNull { it.playerNumber == summary.activePlayerNumber && !it.eliminated }
        ?.takeIf { summary.state == TableState.RUNNING }

/** The starter's first turn: what it may not do. */
private fun firstTurnNote(holder: TablePlayer, summary: TableSummary): String? {
    if (holder.playerNumber != summary.starterPlayerNumber || holder.turnsCompleted != 0L) return null
    return when {
        summary.settings.profile.pokemon -> "First turn: no attack and no Supporter"
        summary.settings.profile.lorcana -> "First turn: skip your draw"
        else -> null
    }
}

/** Room [TurnPanelLines] takes under the number, in dp. */
internal fun turnPanelHeight(unit: TableUnit, summary: TableSummary, narrow: Boolean): Float {
    val holder = turnHolder(unit, summary)
    val actions = TurnAction.of(summary.settings.profile).size
    return (if (unit.members.any { it.turn.conditions.isNotEmpty() }) 26f else 0f) +
        (if (holder != null && actions > 0) (if (narrow && actions > 2) 96f else 48f) else 0f) +
        (if (holder != null && firstTurnNote(holder, summary) != null) 22f else 0f)
}

/**
 * Under the number: the Active Pokémon's Special Conditions and, while it's this
 * player's turn, the first-turn rule and the once-per-turn actions to tick off.
 */
@Composable
internal fun TurnPanelLines(unit: TableUnit, summary: TableSummary, onMark: (TablePlayer, String, Int) -> Unit, modifier: Modifier) {
    val p = palette
    Column(modifier, horizontalAlignment = Alignment.CenterHorizontally, verticalArrangement = Arrangement.spacedBy(2.dp)) {
        unit.members.forEach { member ->
            val conditions = member.turn.conditions
            if (conditions.isNotEmpty()) {
                Text(SpecialCondition.entries.filter { it in conditions }.joinToString(" · ") { it.label },
                    color = p.text, style = MaterialTheme.typography.labelLarge, fontWeight = FontWeight.SemiBold,
                    modifier = Modifier.background(p.surface.copy(alpha = .85f), RoundedCornerShape(99.dp))
                        .padding(horizontal = 10.dp, vertical = 2.dp))
            }
        }
        val holder = turnHolder(unit, summary) ?: return@Column
        firstTurnNote(holder, summary)?.let {
            Text(it, color = p.text, style = MaterialTheme.typography.labelMedium, textAlign = TextAlign.Center)
        }
        TurnActionChips(holder, summary.settings.profile, onMark)
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun TurnActionChips(holder: TablePlayer, profile: GameProfile, onMark: (TablePlayer, String, Int) -> Unit) {
    val p = palette
    FlowRow(horizontalArrangement = Arrangement.spacedBy(6.dp, Alignment.CenterHorizontally)) {
        TurnAction.of(profile).forEach { action ->
            val done = action in holder.turn.turnActions
            FilterChip(
                selected = done,
                onClick = { onMark(holder, StandaloneGame.ACTION_KIND + action.name, if (done) 0 else 1) },
                label = { Text(if (done) "✓ ${action.label}" else action.label) },
                colors = FilterChipDefaults.filterChipColors(containerColor = p.surface.copy(alpha = .85f), selectedContainerColor = p.accentSoft),
            )
        }
    }
}

/**
 * The player's menu in Pokémon and Lorcana: this turn's actions, Pokémon's
 * Special Conditions, and the ways a game ends besides the number. [onAction]
 * sends `win` or `eliminate` for that player.
 */
@OptIn(ExperimentalLayoutApi::class)
@Composable
internal fun CardGameMenuItems(unit: TableUnit, summary: TableSummary, onMark: (TablePlayer, String, Int) -> Unit,
    onAction: (TablePlayer, String) -> Unit) {
    val p = palette
    if (summary.state != TableState.RUNNING && summary.state != TableState.PAUSED) return
    val member = unit.members.firstOrNull { !it.eliminated } ?: return
    val profile = summary.settings.profile
    com.turnhub.android.ui.components.DisclosureCard(if (profile.pokemon) "Pokémon" else "Lorcana", initiallyExpanded = true) {
        turnHolder(unit, summary)?.let { holder ->
            Text("This turn", color = p.muted, style = MaterialTheme.typography.labelLarge)
            TurnActionChips(holder, profile, onMark)
        }
        if (profile.pokemon) {
            Text("Special Conditions on your Active Pokémon", color = p.muted, style = MaterialTheme.typography.labelLarge)
            FlowRow(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                SpecialCondition.entries.forEach { condition ->
                    val on = condition in member.turn.conditions
                    FilterChip(
                        selected = on,
                        onClick = { onMark(member, StandaloneGame.CONDITION_KIND + condition.name, if (on) 0 else 1) },
                        label = { Text(if (on) "✓ ${condition.label}" else condition.label) },
                        colors = FilterChipDefaults.filterChipColors(selectedContainerColor = p.accentSoft),
                    )
                }
            }
            Text("Asleep, Confused and Paralyzed replace each other. Pokémon Checkup between turns flips the coins " +
                "and ends conditions; clear them here when the Pokémon retreats, evolves or is Knocked Out.",
                color = p.faint, style = MaterialTheme.typography.bodySmall)
            Text("Other ways to win", color = p.muted, style = MaterialTheme.typography.labelLarge)
            ArmedButton("Opponent has no Pokémon left in play", "Tap again to win", Tone.GOOD) { onAction(member, "win") }
            ArmedButton("Opponent can't draw at the start of their turn", "Tap again to win", Tone.GOOD) { onAction(member, "win") }
        } else {
            Text("Reaching ${summary.settings.startingLife} lore wins at once. A player who has to draw from an empty deck loses.",
                color = p.faint, style = MaterialTheme.typography.bodySmall)
            ArmedButton("I had to draw from an empty deck", "Tap again: you're out", Tone.BAD) { onAction(member, "eliminate") }
        }
    }
}

/** What the last Pokémon Checkup did, one line each. */
@Composable
internal fun PokemonCheckupLines(lines: List<String>) {
    lines.forEach { Text(it, color = palette.text) }
}
