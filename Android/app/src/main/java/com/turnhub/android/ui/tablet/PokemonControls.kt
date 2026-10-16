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
import com.turnhub.android.domain.PokemonAction
import com.turnhub.android.domain.SpecialCondition
import com.turnhub.android.domain.TablePlayer
import com.turnhub.android.domain.TableSummary
import com.turnhub.android.protocol.TableState
import com.turnhub.android.standalone.StandaloneGame
import com.turnhub.android.ui.components.Tone
import com.turnhub.android.ui.theme.palette

// Pokémon on the full-screen table (phone-hosted games): prizes are the panel's
// number; these are the reminders around it. Everything is text, never color alone.

/** The player whose once-per-turn actions this panel shows: its member holding the turn. */
private fun turnHolder(unit: TableUnit, summary: TableSummary): TablePlayer? =
    unit.members.firstOrNull { it.playerNumber == summary.activePlayerNumber && !it.eliminated }
        ?.takeIf { summary.state == TableState.RUNNING }

/** The starter's first turn: no attack and no Supporter. */
private fun firstTurn(holder: TablePlayer, summary: TableSummary): Boolean =
    holder.playerNumber == summary.starterPlayerNumber && holder.turnsCompleted == 0L

/** Room [PokemonPanelLines] takes under the prize count, in dp. */
internal fun pokemonPanelHeight(unit: TableUnit, summary: TableSummary, narrow: Boolean): Float {
    val holder = turnHolder(unit, summary)
    return (if (unit.members.any { it.pokemon.conditions.isNotEmpty() }) 26f else 0f) +
        (if (holder != null) (if (narrow) 96f else 48f) else 0f) +
        (if (holder != null && firstTurn(holder, summary)) 22f else 0f)
}

/**
 * Under the prize count: the Active Pokémon's Special Conditions and, while it's
 * this player's turn, the once-per-turn actions to tick off.
 */
@OptIn(ExperimentalLayoutApi::class)
@Composable
internal fun PokemonPanelLines(unit: TableUnit, summary: TableSummary, onMark: (TablePlayer, String, Int) -> Unit, modifier: Modifier) {
    val p = palette
    Column(modifier, horizontalAlignment = Alignment.CenterHorizontally, verticalArrangement = Arrangement.spacedBy(2.dp)) {
        unit.members.forEach { member ->
            val conditions = member.pokemon.conditions
            if (conditions.isNotEmpty()) {
                Text(SpecialCondition.entries.filter { it in conditions }.joinToString(" · ") { it.label },
                    color = p.text, style = MaterialTheme.typography.labelLarge, fontWeight = FontWeight.SemiBold,
                    modifier = Modifier.background(p.surface.copy(alpha = .85f), RoundedCornerShape(99.dp))
                        .padding(horizontal = 10.dp, vertical = 2.dp))
            }
        }
        val holder = turnHolder(unit, summary) ?: return@Column
        if (firstTurn(holder, summary)) {
            Text("First turn: no attack and no Supporter", color = p.text, style = MaterialTheme.typography.labelMedium,
                textAlign = TextAlign.Center)
        }
        TurnActionChips(holder, onMark)
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun TurnActionChips(holder: TablePlayer, onMark: (TablePlayer, String, Int) -> Unit) {
    val p = palette
    FlowRow(horizontalArrangement = Arrangement.spacedBy(6.dp, Alignment.CenterHorizontally)) {
        PokemonAction.entries.forEach { action ->
            val done = action in holder.pokemon.turnActions
            FilterChip(
                selected = done,
                onClick = { onMark(holder, StandaloneGame.ACTION_KIND + action.name, if (done) 0 else 1) },
                label = { Text(if (done) "✓ ${action.label}" else action.label) },
                colors = FilterChipDefaults.filterChipColors(containerColor = p.surface.copy(alpha = .85f), selectedContainerColor = p.accentSoft),
            )
        }
    }
}

/** The player's menu: this turn's actions, Special Conditions and the other ways to win. */
@OptIn(ExperimentalLayoutApi::class)
@Composable
internal fun PokemonMenuItems(unit: TableUnit, summary: TableSummary, onMark: (TablePlayer, String, Int) -> Unit,
    onWin: (TablePlayer) -> Unit) {
    val p = palette
    if (summary.state != TableState.RUNNING && summary.state != TableState.PAUSED) return
    val member = unit.members.firstOrNull { !it.eliminated } ?: return
    com.turnhub.android.ui.components.DisclosureCard("Pokémon", initiallyExpanded = true) {
        turnHolder(unit, summary)?.let { holder ->
            Text("This turn", color = p.muted, style = MaterialTheme.typography.labelLarge)
            TurnActionChips(holder, onMark)
        }
        Text("Special Conditions on your Active Pokémon", color = p.muted, style = MaterialTheme.typography.labelLarge)
        FlowRow(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            SpecialCondition.entries.forEach { condition ->
                val on = condition in member.pokemon.conditions
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
        ArmedButton("Opponent has no Pokémon left in play", "Tap again to win", Tone.GOOD) { onWin(member) }
        ArmedButton("Opponent can't draw at the start of their turn", "Tap again to win", Tone.GOOD) { onWin(member) }
    }
}

/** What the last Pokémon Checkup did, one line each. */
@Composable
internal fun PokemonCheckupLines(lines: List<String>) {
    lines.forEach { Text(it, color = palette.text) }
}
