package com.turnhub.android.ui.tablet

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.ui.semantics.heading
import com.turnhub.android.domain.DeathMayDie
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.widthIn
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.liveRegion
import androidx.compose.ui.semantics.LiveRegionMode
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.FilterChip
import androidx.compose.material3.FilterChipDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
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
    profile.deathMayDie -> if (score == 1) "wound" else "wounds"
    else -> null
}

/** What TalkBack reads for a panel's number. */
internal fun scoreDescription(profile: GameProfile, name: String, score: Int, goal: Int): String = when {
    profile.pokemon -> "$name, $score Prize cards left"
    profile.lorcana -> "$name, $score lore of $goal"
    profile.deathMayDie -> "$name, $score ${if (score == 1) "wound" else "wounds"}"
    else -> "$name, life $score"
}

/** The panel's minus and plus halves, for TalkBack. */
internal fun stepLabel(profile: GameProfile, name: String, step: Int, adding: Boolean): String = when {
    profile.pokemon -> if (adding) "$name: put a Prize card back" else "$name: take a Prize card"
    profile.lorcana -> if (adding) "$name: gain $step lore" else "$name: lose $step lore"
    profile.deathMayDie -> if (adding) "$name: add a wound" else "$name: heal a wound"
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
    // One row: where the player is in the round and Take the next turn; two when narrow.
    if (summary.settings.profile.cooperative) return when {
        unit.members.none { roundStatus(it, summary) != null } -> 0f
        narrow -> 112f
        else -> 56f
    }
    val holder = turnHolder(unit, summary)
    val actions = TurnAction.of(summary.settings.profile).size
    return (if (unit.members.any { it.turn.conditions.isNotEmpty() }) 26f else 0f) +
        (if (holder != null && actions > 0) (if (narrow && actions > 2) 96f else 48f) else 0f) +
        (if (holder != null && firstTurnNote(holder, summary) != null) 22f else 0f)
}

/**
 * Room every panel keeps for [TurnPanelLines] while the game runs, in dp: the action row is
 * reserved whether or not this panel holds the turn, so passing never moves the number.
 */
internal fun turnPanelReserve(unit: TableUnit, summary: TableSummary, narrow: Boolean): Float {
    val shown = turnPanelHeight(unit, summary, narrow)
    if (summary.settings.profile.cooperative || summary.state != TableState.RUNNING || unit.out) return shown
    val actions = TurnAction.of(summary.settings.profile).size
    val row = if (actions == 0) 0f else if (narrow && actions > 2) 96f else 48f
    return maxOf(shown, row + if (unit.members.any { it.turn.conditions.isNotEmpty() }) 26f else 0f)
}

/**
 * Under the number: the Active Pokémon's Special Conditions and, while it's this
 * player's turn, the first-turn rule and the once-per-turn actions to tick off.
 */
@Composable
internal fun TurnPanelLines(unit: TableUnit, summary: TableSummary, onMark: (TablePlayer, String, Int) -> Unit, modifier: Modifier, enabled: Boolean = true) {
    val p = palette
    if (summary.settings.profile.cooperative) {
        RoundStatus(unit, summary, onMark, modifier)
        return
    }
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
        TurnActionChips(holder, summary.settings.profile, onMark, enabled)
    }
}

/** Where this player is in the round, in words: never shown by color or the panel's glow alone. */
internal fun roundStatus(player: TablePlayer, summary: TableSummary): String? = when {
    summary.state == TableState.GAME_OVER || summary.state == TableState.LOBBY -> null
    player.eliminated -> null
    summary.activePlayerNumber == player.playerNumber -> "Taking their turn"
    player.actedThisRound -> "✓ Turn taken"
    summary.nextTurnPlayerNumber == player.playerNumber -> "Up next"
    else -> STILL_TO_GO
}

private const val STILL_TO_GO = "Still to go"

/**
 * A cooperative game's panel: whether this player has taken their turn this round. There
 * is no fixed order, so a player still to go can stage the next turn.
 */
@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun RoundStatus(unit: TableUnit, summary: TableSummary, onMark: (TablePlayer, String, Int) -> Unit, modifier: Modifier) {
    val p = palette
    val member = unit.members.first()
    val status = roundStatus(member, summary) ?: return
    FlowRow(modifier, horizontalArrangement = Arrangement.spacedBy(10.dp, Alignment.CenterHorizontally),
        verticalArrangement = Arrangement.spacedBy(6.dp)) {
        Text(status, color = if (member.actedThisRound) p.muted else p.text, style = MaterialTheme.typography.labelLarge,
            fontWeight = FontWeight.SemiBold,
            modifier = Modifier.background(if (member.actedThisRound) p.surface.copy(alpha = .85f) else p.accentSoft, RoundedCornerShape(99.dp))
                .padding(horizontal = 12.dp, vertical = 4.dp))
        if ((status == STILL_TO_GO || summary.nextTurnPlayerNumber == member.playerNumber) && summary.state == TableState.RUNNING) {
            com.turnhub.android.ui.components.ToneButton(if (summary.nextTurnPlayerNumber == member.playerNumber) "Cancel next turn" else "Take the next turn",
                { onMark(member, StandaloneGame.TAKE_TURN_KIND, 1) })
        }
    }
}

/**
 * Death May Die's panel: wounds and stress as two matching counters side by side, with
 * where the player is in the round under them. [wounds] includes taps not yet sent.
 */
@OptIn(ExperimentalLayoutApi::class)
@Composable
internal fun InvestigatorTracks(
    unit: TableUnit,
    summary: TableSummary,
    wounds: Int,
    onWound: (Int) -> Unit,
    onMark: (TablePlayer, String, Int) -> Unit,
    modifier: Modifier,
    /** Sends `eliminate` for the player; null leaves the sixth-wound alert without its button. */
    onAction: ((TablePlayer, String) -> Unit)? = null,
) {
    val member = unit.members.first()
    val live = summary.state == TableState.RUNNING && !unit.out
    Column(modifier, horizontalAlignment = Alignment.CenterHorizontally, verticalArrangement = Arrangement.spacedBy(10.dp)) {
        FlowRow(horizontalArrangement = Arrangement.spacedBy(10.dp, Alignment.CenterHorizontally),
            verticalArrangement = Arrangement.spacedBy(8.dp)) {
            Tracker("Wounds", wounds, DeathMayDie.DEATH_WOUNDS, member.label, "wound", live) { onWound(it) }
            Tracker("Stress", member.stress, DeathMayDie.MAX_STRESS, member.label, "stress", live) { onMark(member, StandaloneGame.STRESS_KIND, it) }
            Tracker("Fire", member.dashboard.fire, com.turnhub.android.domain.InvestigatorDashboard.MAX_FIRE, member.label, "fire token", live,
                alarmAtMax = false) { onMark(member, StandaloneGame.FIRE_KIND, it) }
        }
        if (member.dashboard.fire > 0 && live) FireReminder(member, onMark)
        if (live && (wounds > 0 || member.stress > 0)) RestButton(member, wounds, onWound) { onMark(member, StandaloneGame.STRESS_KIND, it) }
        if (wounds >= DeathMayDie.DEATH_WOUNDS && !unit.out) DeathAlert(member, summary, onAction)
        RoundStatus(unit, summary, onMark, Modifier)
    }
}

/**
 * The sixth wound kills (owner, 2026-10-19). It never removes anyone by itself: the alert
 * asks the table to confirm with an armed button, so a mis-tap on + isn't a death.
 */
@Composable
private fun DeathAlert(member: TablePlayer, summary: TableSummary, onAction: ((TablePlayer, String) -> Unit)?) {
    val p = palette
    val losesGame = summary.elder?.takeIf { it.inUse }?.summoned == false
    Column(Modifier.fillMaxWidth().background(p.surface, RoundedCornerShape(14.dp))
        .border(2.dp, p.bad, RoundedCornerShape(14.dp)).padding(12.dp),
        verticalArrangement = Arrangement.spacedBy(8.dp)) {
        Text("☠ ${DeathMayDie.DEATH_WOUNDS} wounds: ${member.investigator ?: member.label} dies", color = p.bad,
            style = MaterialTheme.typography.titleMedium, fontWeight = FontWeight.Black,
            modifier = Modifier.semantics { heading(); liveRegion = LiveRegionMode.Assertive })
        Text(if (losesGame) "The Elder One is still on the Summoning track, so this loses the game. Remove a wound if it was a mistake."
            else "Confirm the death, or remove a wound if it was a mistake.", color = p.text, style = MaterialTheme.typography.bodyMedium)
        onAction?.let { act ->
            ArmedButton("${member.label} died", if (losesGame) "Tap again: the investigators lose" else "Tap again: ${member.label} is out",
                Tone.BAD) { act(member, "eliminate") }
        }
    }
}

/**
 * The Rest action (Safe space only): heal 3, split between stress and wounds as the player
 * likes. It sends the same −1 steps as the counters, so nothing new reaches the engine.
 */
@Composable
private fun RestButton(member: TablePlayer, wounds: Int, onWound: (Int) -> Unit, onStress: (Int) -> Unit) {
    var open by remember { mutableStateOf(false) }
    com.turnhub.android.ui.components.ToneButton("Rest: heal 3", { open = true })
    if (!open) return
    val stress = member.stress
    // Every split of 3 heals the player can use; with less to heal, the most that can be.
    val splits = (0..3).map { s -> minOf(s, stress) to minOf(3 - s, wounds) }.distinct()
        .filter { (s, w) -> s + w == minOf(3, stress + wounds) }
    androidx.compose.material3.AlertDialog(
        onDismissRequest = { open = false },
        title = { Text("${member.investigator ?: member.label} rests") },
        text = {
            Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                Text("Safe space only. Heal 3 in any mix of stress and wounds.", color = palette.muted)
                splits.forEach { (s, w) ->
                    com.turnhub.android.ui.components.ToneButton(listOfNotNull(
                        "$s stress".takeIf { s > 0 }, "$w wound${if (w == 1) "" else "s"}".takeIf { w > 0 }).joinToString(" + "), {
                        repeat(s) { onStress(-1) }
                        repeat(w) { onWound(-1) }
                        open = false
                    }, Modifier.fillMaxWidth())
                }
            }
        },
        confirmButton = { androidx.compose.material3.TextButton({ open = false }) { Text("Cancel") } },
    )
}

/** End of Turn, step 2 (Fire!): one black die per fire token on the board, then they're discarded. */
@Composable
private fun FireReminder(member: TablePlayer, onMark: (TablePlayer, String, Int) -> Unit) {
    val p = palette
    val fire = member.dashboard.fire
    Column(Modifier.fillMaxWidth().background(p.surface, RoundedCornerShape(14.dp))
        .border(2.dp, p.warn, RoundedCornerShape(14.dp)).padding(12.dp),
        verticalArrangement = Arrangement.spacedBy(8.dp)) {
        Text("🔥 On fire: $fire token${if (fire == 1) "" else "s"}", color = p.text,
            style = MaterialTheme.typography.titleMedium, fontWeight = FontWeight.Black, modifier = Modifier.semantics { heading() })
        Text("At End of Turn, roll $fire black ${if (fire == 1) "die" else "dice"}, take what they show, then discard the fire.",
            color = p.text, style = MaterialTheme.typography.bodyMedium)
        com.turnhub.android.ui.components.ToneButton("Rolled: discard the fire", { onMark(member, StandaloneGame.FIRE_KIND, -fire) },
            Modifier.fillMaxWidth(), tone = Tone.WARN)
    }
}

/** One counter: −, its name over its number out of [max], +. Every counter has the same width, so a pair lines up. */
@Composable
private fun Tracker(name: String, value: Int, max: Int, player: String, noun: String, live: Boolean, alarmAtMax: Boolean = true,
                    onStep: (Int) -> Unit) {
    val p = palette
    Row(Modifier.background(p.surface.copy(alpha = .9f), RoundedCornerShape(99.dp)).padding(4.dp),
        verticalAlignment = Alignment.CenterVertically) {
        StepButton("−", "$player: remove a $noun", live && value > 0) { onStep(-1) }
        Column(Modifier.widthIn(min = 76.dp).semantics(mergeDescendants = true) { liveRegion = LiveRegionMode.Polite },
            horizontalAlignment = Alignment.CenterHorizontally) {
            Text(name, color = p.muted, style = MaterialTheme.typography.labelMedium)
            Row(verticalAlignment = Alignment.Bottom) {
                Text("$value", color = if (alarmAtMax && value >= max) p.bad else p.text, style = MaterialTheme.typography.headlineSmall, fontWeight = FontWeight.Black)
                Text(" / $max", color = p.muted, style = MaterialTheme.typography.labelLarge, modifier = Modifier.padding(bottom = 3.dp))
            }
        }
        StepButton("+", "$player: add a $noun", live && value < max) { onStep(1) }
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun TurnActionChips(holder: TablePlayer, profile: GameProfile, onMark: (TablePlayer, String, Int) -> Unit, enabled: Boolean = true) {
    val p = palette
    FlowRow(horizontalArrangement = Arrangement.spacedBy(6.dp, Alignment.CenterHorizontally)) {
        TurnAction.of(profile).forEach { action ->
            val done = action in holder.turn.turnActions
            FilterChip(
                selected = done,
                enabled = enabled,
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
    if (profile.deathMayDie) {
        DeathMayDieMenuItems(member, summary, onAction)
        return
    }
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

/**
 * The player's menu in Death May Die: their investigator, where the round
 * stands, and how the game ends. Everyone wins or loses together (rulebook,
 * "Winning and Losing"): killing the Elder One wins; an investigator who dies
 * or goes insane before it is summoned loses the game, afterwards only they are out.
 */
@Composable
private fun DeathMayDieMenuItems(member: TablePlayer, summary: TableSummary, onAction: (TablePlayer, String) -> Unit) {
    val p = palette
    com.turnhub.android.ui.components.DisclosureCard("Death May Die", initiallyExpanded = true) {
        Text(member.investigator?.let { "Investigator: $it" } ?: "No investigator chosen", color = p.text)
        summary.round?.let { round ->
            val waiting = summary.players.filter { !it.eliminated && !it.actedThisRound && it.playerNumber != summary.activePlayerNumber }
            Text("Round $round · " + if (waiting.isEmpty()) "the last turn of the round" else "still to go: ${waiting.joinToString { it.label }}",
                color = p.muted)
        }
        Text("Stress stops at ${DeathMayDie.MAX_STRESS}. The ${DeathMayDie.DEATH_WOUNDS}th wound kills: confirm it below or on the panel.",
            color = p.faint, style = MaterialTheme.typography.bodySmall)
        ArmedButton("We killed the Elder One", "Tap again: the investigators win", Tone.GOOD) { onAction(member, "win") }
        // With the Elder One tracked in its tab, the engine knows whether this loses the game.
        val elder = summary.elder?.takeIf { it.inUse }
        val losesGame = elder?.summoned == false
        ArmedButton("${member.label} died or went insane",
            if (losesGame) "Tap again: the investigators lose" else "Tap again: ${member.label} is out", Tone.BAD) { onAction(member, "eliminate") }
        Text(when {
            elder == null -> "With the Elder One on the board, the others play on. Before it is summoned, that loses the game."
            losesGame -> "The Elder One is still on the Summoning track, so this loses the game."
            else -> "The Elder One is on the board: the others play on."
        }, color = p.faint, style = MaterialTheme.typography.bodySmall)
        ArmedButton("We lost", "Tap again: the investigators lose", Tone.BAD) { onAction(member, "draw") }
        Text("An investigator fell before the Elder One was summoned, or the Summoning track reached its last space.",
            color = p.faint, style = MaterialTheme.typography.bodySmall)
    }
}

/** What the last Pokémon Checkup did, one line each. */
@Composable
internal fun PokemonCheckupLines(lines: List<String>) {
    lines.forEach { Text(it, color = palette.text) }
}
