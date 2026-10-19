package com.turnhub.android.ui.tablet

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.semantics.LiveRegionMode
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.clearAndSetSemantics
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.liveRegion
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.unit.dp
import com.turnhub.android.domain.ElderOneTrack
import com.turnhub.android.domain.StoryGroup
import com.turnhub.android.domain.DeathMayDieStory
import com.turnhub.android.protocol.TableState
import com.turnhub.android.standalone.StandaloneState
import com.turnhub.android.standalone.StandaloneTable
import com.turnhub.android.ui.components.AccentButton
import com.turnhub.android.ui.components.BrassCard
import com.turnhub.android.ui.components.Tone
import com.turnhub.android.ui.components.ToneButton
import com.turnhub.android.ui.theme.palette

/**
 * The Elder One tab (Death May Die on this device): which Elder One and episode the table
 * plays, then the Story board during the game: Summoning symbols, the Summoning track, the
 * ritual and the Elder One's stages ([ElderOneTrack]). Card effects stay on the physical
 * cards; every control here is a reminder the table can correct.
 */
@Composable
fun ElderOneTab(state: StandaloneState, table: StandaloneTable) {
    val game = state.game
    val elder = game.elder
    val playing = game.state == TableState.RUNNING || game.state == TableState.PAUSED
    Column(
        Modifier.widthIn(max = 720.dp).fillMaxWidth().verticalScroll(rememberScrollState()).padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(14.dp),
    ) {
        StoryChoices(elder, game.state != TableState.GAME_OVER, table)
        when {
            playing -> {
                SummoningCard(elder, game.state == TableState.RUNNING, table)
                if (elder.summoned) StageCard(elder, game.state == TableState.RUNNING, table)
                else RitualCard(elder, game.state == TableState.RUNNING, table)
                BrassCard { DeathMayDieTurnPhasesButton() }
            }
            game.state == TableState.GAME_OVER -> BrassCard {
                Text(elder.headline(), color = palette.text, style = MaterialTheme.typography.titleMedium)
                Text(if (game.winners.isNotEmpty()) "The investigators won." else "The investigators lost.", color = palette.muted)
            }
            else -> BrassCard {
                Text("The Story board", color = palette.text, style = MaterialTheme.typography.titleLarge, modifier = Modifier.semantics { heading() })
                Text("Once the game starts, this tab follows the Summoning track, the ritual and the Elder One's stages. " +
                    "Choosing an Elder One here also means an investigator lost before it is summoned ends the game, as the rules say.",
                    color = palette.muted)
            }
        }
    }
}

@Composable
private fun StoryChoices(elder: ElderOneTrack, enabled: Boolean, table: StandaloneTable) {
    val p = palette
    var picking by rememberSaveable { mutableStateOf<String?>(null) }
    BrassCard {
        Text("Elder One and episode", color = p.text, style = MaterialTheme.typography.titleLarge, modifier = Modifier.semantics { heading() })
        StoryChoice("Elder One", elder.elderOne, enabled) { picking = "elder" }
        StoryChoice("Episode", elder.episode, enabled) { picking = "episode" }
    }
    when (picking) {
        "elder" -> StoryPickerDialog("Elder One", DeathMayDieStory.elderOnes, elder.elderOne,
            { table.setElderOne(it); picking = null }) { picking = null }
        "episode" -> StoryPickerDialog("Episode", DeathMayDieStory.episodes, elder.episode,
            { table.setEpisode(it); picking = null }) { picking = null }
    }
}

@Composable
private fun StoryChoice(label: String, value: String?, enabled: Boolean, onPick: () -> Unit) {
    val p = palette
    Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(12.dp)) {
        Column(Modifier.weight(1f)) {
            Text(label, color = p.muted, style = MaterialTheme.typography.labelLarge)
            Text(value ?: "Not chosen", color = if (value == null) p.faint else p.text, style = MaterialTheme.typography.titleMedium,
                fontWeight = FontWeight.SemiBold)
        }
        ToneButton(if (value == null) "Choose" else "Change", onPick, enabled = enabled)
    }
}

/** The Summoning track's 8 spaces and the Mythos discard's Summoning symbols. */
@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun SummoningCard(elder: ElderOneTrack, running: Boolean, table: StandaloneTable) {
    val p = palette
    BrassCard {
        Text("Summoning track", color = p.text, style = MaterialTheme.typography.titleLarge, modifier = Modifier.semantics { heading() })
        val marker = if (elder.summoned) "Progression token" else "Elder One"
        Text("$marker on space ${elder.space} of ${ElderOneTrack.LAST_SPACE}", color = p.text, style = MaterialTheme.typography.titleMedium,
            fontWeight = FontWeight.SemiBold, modifier = Modifier.semantics { liveRegion = LiveRegionMode.Polite })
        TrackSpaces(elder.space)
        Text("Spaces 1 to 4 are green, 5 to 7 red. Entering the first red space summons the Elder One; " +
            "the Progression token reaching space 8 loses the game.", color = p.faint, style = MaterialTheme.typography.bodySmall)
        FlowRow(horizontalArrangement = Arrangement.spacedBy(10.dp), verticalArrangement = Arrangement.spacedBy(8.dp),
            itemVerticalAlignment = Alignment.CenterVertically) {
            StepButton("−", "Move the $marker back a space", running && elder.space > ElderOneTrack.START_SPACE) {
                table.changeElder { it.advance(-1) }
            }
            if (elder.space + 1 >= ElderOneTrack.LAST_SPACE) {
                if (running) ArmedButton("Advance to the last space", "Tap again: the investigators lose", Tone.BAD) { table.changeElder { it.advance(1) } }
            } else ToneButton("Advance the $marker", { table.changeElder { it.advance(1) } }, enabled = running)
        }

        Text("Summoning symbols", color = p.text, style = MaterialTheme.typography.titleMedium, fontWeight = FontWeight.SemiBold,
            modifier = Modifier.padding(top = 4.dp))
        Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(10.dp)) {
            StepButton("−", "Remove a Summoning symbol", running && elder.symbols > 0) { table.changeElder { it.symbol(-1) } }
            Text("${elder.symbols} of ${ElderOneTrack.SYMBOLS_TO_ADVANCE} in the Mythos discards", color = p.text,
                modifier = Modifier.semantics { liveRegion = LiveRegionMode.Polite })
            val ends = elder.symbols + 1 >= ElderOneTrack.SYMBOLS_TO_ADVANCE && elder.space + 1 >= ElderOneTrack.LAST_SPACE
            if (!ends) StepButton("+", "Add a Summoning symbol", running) { table.changeElder { it.symbol(1) } }
        }
        if (running && elder.symbols + 1 >= ElderOneTrack.SYMBOLS_TO_ADVANCE && elder.space + 1 >= ElderOneTrack.LAST_SPACE) {
            ArmedButton("Third Summoning symbol", "Tap again: the investigators lose", Tone.BAD) { table.changeElder { it.symbol(1) } }
        }
        Text("Count the discarded Mythos cards that show one. The third advances the Elder One and the discards are shuffled back " +
            "into the Mythos deck; resolve the Elder One's advance effects first, then the episode's.",
            color = p.faint, style = MaterialTheme.typography.bodySmall)
    }
}

/** Eight numbered circles; the marker's space is filled and named, never shown by color alone. */
@Composable
private fun TrackSpaces(space: Int) {
    val p = palette
    Row(Modifier.fillMaxWidth().clearAndSetSemantics { }, horizontalArrangement = Arrangement.spacedBy(4.dp)) {
        (ElderOneTrack.START_SPACE..ElderOneTrack.LAST_SPACE).forEach { n ->
            val red = n >= ElderOneTrack.FIRST_RED_SPACE && n < ElderOneTrack.LAST_SPACE
            val tint = when {
                n == ElderOneTrack.LAST_SPACE -> p.text
                red -> p.bad
                else -> p.good
            }
            val here = n == space
            Box(Modifier.weight(1f), contentAlignment = Alignment.Center) {
                Box(
                    Modifier.size(34.dp).clip(CircleShape)
                        .background(if (here) tint else tint.copy(alpha = .18f))
                        .border(if (here) 3.dp else 1.dp, if (here) p.text else tint.copy(alpha = .6f), CircleShape),
                    contentAlignment = Alignment.Center,
                ) {
                    Text(if (n == ElderOneTrack.LAST_SPACE) "✕" else "$n", color = if (here) p.surface else p.text,
                        fontWeight = if (here) FontWeight.Black else FontWeight.Normal)
                }
            }
        }
    }
}

/** Before it is summoned: disrupting the ritual summons it, or it can arrive another way. */
@Composable
private fun RitualCard(elder: ElderOneTrack, running: Boolean, table: StandaloneTable) {
    val p = palette
    BrassCard {
        Text("The ritual", color = p.text, style = MaterialTheme.typography.titleLarge, modifier = Modifier.semantics { heading() })
        Text("Stage 1 is on top: the Elder One is on the Summoning track. Until it is summoned, an investigator who dies or goes insane loses the game.",
            color = p.muted)
        Text("Do your episode's task to disrupt the ritual. That summons the Elder One at the end of the turn and makes it mortal.",
            color = p.muted)
        AccentButton("We disrupted the ritual", { table.changeElder { it.disrupt() } }, Modifier.fillMaxWidth(), enabled = running)
        TextButton({ table.changeElder { it.summon() } }, enabled = running) { Text("It was summoned another way") }
    }
}

/** Once summoned: the revealed stage, its health and wounds, and how it falls. */
@Composable
private fun StageCard(elder: ElderOneTrack, running: Boolean, table: StandaloneTable) {
    val p = palette
    val stage = ElderOneTrack.stageName(elder.stage)
    BrassCard(highlight = if (elder.vulnerable) p.bad else null) {
        Text("${elder.elderOne ?: "The Elder One"} · $stage", color = p.text, style = MaterialTheme.typography.titleLarge,
            modifier = Modifier.semantics { heading() })
        Text("On the board. Its dice are all the dice on its revealed Stage cards, and every revealed stage's effects stay in play.",
            color = p.muted)
        if (!elder.ritualDisrupted) {
            Text("It can't be attacked or wounded until the ritual is disrupted.", color = p.text, fontWeight = FontWeight.SemiBold)
            AccentButton("We disrupted the ritual", { table.changeElder { it.disrupt() } }, Modifier.fillMaxWidth(), enabled = running)
            if (elder.stage == 2) TextButton({ table.changeElder { it.unsummon() } }, enabled = running) { Text("Undo: it isn't summoned yet") }
            return@BrassCard
        }
        HealthField(elder, running, table)
        Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(10.dp)) {
            StepButton("−", "Remove a wound from the Elder One", running && elder.stageWounds > 0) { table.changeElder { it.wound(-1) } }
            Column(Modifier.width(120.dp).semantics(mergeDescendants = true) { liveRegion = LiveRegionMode.Polite },
                horizontalAlignment = Alignment.CenterHorizontally) {
                Text("Wounds", color = p.muted, style = MaterialTheme.typography.labelMedium)
                Text("${elder.stageWounds}${elder.health?.let { " / $it" } ?: ""}", color = p.text,
                    style = MaterialTheme.typography.headlineSmall, fontWeight = FontWeight.Black)
            }
            StepButton("+", "Add a wound to the Elder One", running && elder.stageWounds < ElderOneTrack.MAX_WOUNDS) { table.changeElder { it.wound(1) } }
        }
        if (elder.stageBeaten) Text("$stage has taken its health in wounds: it is defeated.", color = p.text, fontWeight = FontWeight.SemiBold)
        if (elder.stage < ElderOneTrack.FINAL_STAGE) {
            val next = ElderOneTrack.stageName(elder.stage + 1)
            val defeat = { table.changeElder { it.defeatStage() } }
            if (elder.stageBeaten) AccentButton("Reveal $next", defeat, Modifier.fillMaxWidth(), enabled = running)
            else ToneButton("$stage defeated: reveal $next", defeat, Modifier.fillMaxWidth(), enabled = running)
            Text("Extra wounds don't carry over. Resolve $next's When Revealed effect.", color = p.faint, style = MaterialTheme.typography.bodySmall)
        } else if (running) {
            ArmedButton("We killed the Elder One", "Tap again: the investigators win", Tone.GOOD) { table.killElderOne() }
        }
        if (elder.stageWounds == 0) TextButton({ table.changeElder { it.undisrupt() } }, enabled = running) { Text("Undo: the ritual isn't disrupted") }
    }
}

/** The health printed on the current stage, typed once as the stage is revealed. */
@Composable
private fun HealthField(elder: ElderOneTrack, running: Boolean, table: StandaloneTable) {
    var text by rememberSaveable(elder.stage) { mutableStateOf(elder.health?.toString().orEmpty()) }
    LaunchedEffect(elder.health) { if (text.toIntOrNull() != elder.health) text = elder.health?.toString().orEmpty() }
    OutlinedTextField(text, { value ->
        text = value.filter(Char::isDigit).take(2)
        table.changeElder { it.withHealth(text.toIntOrNull()?.takeIf { n -> n > 0 }) }
    }, label = { Text("Health printed on ${ElderOneTrack.stageName(elder.stage)}") }, singleLine = true, enabled = running,
        keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number), modifier = Modifier.fillMaxWidth())
}

/** Picks an Elder One or episode by box, or takes a typed name; Clear unsets it. */
@Composable
private fun StoryPickerDialog(title: String, groups: List<StoryGroup>, current: String?, onSave: (String?) -> Unit, onDismiss: () -> Unit) {
    val p = palette
    var query by rememberSaveable { mutableStateOf("") }
    val search = query.trim()
    val matches = groups.mapNotNull { group ->
        group.names.filter { search.isEmpty() || it.contains(search, ignoreCase = true) }.takeIf { it.isNotEmpty() }?.let { group.title to it }
    }
    val typedNew = search.isNotEmpty() && groups.none { g -> g.names.any { it.equals(search, ignoreCase = true) } }
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(title) },
        text = {
            Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                OutlinedTextField(query, { query = it.take(ElderOneTrack.MAX_NAME) }, label = { Text("Search or type a name") },
                    singleLine = true, modifier = Modifier.fillMaxWidth())
                Column(Modifier.weight(1f, fill = false).verticalScroll(rememberScrollState()), verticalArrangement = Arrangement.spacedBy(2.dp)) {
                    if (typedNew) StoryRow("Use “$search”", false) { onSave(search) }
                    if (matches.isEmpty() && !typedNew) Text("Nothing matches.", color = p.muted)
                    matches.forEach { (group, names) ->
                        Text(group, color = p.muted, style = MaterialTheme.typography.labelLarge,
                            modifier = Modifier.padding(top = 8.dp).semantics { heading() })
                        names.forEach { name -> StoryRow(name, name == current) { onSave(name) } }
                    }
                }
                Text("From the community Death May Die wiki. Any name can be typed in.", color = p.faint, style = MaterialTheme.typography.bodySmall)
            }
        },
        confirmButton = { TextButton(onDismiss) { Text("Cancel") } },
        dismissButton = { if (current != null) TextButton({ onSave(null) }) { Text("Clear") } },
    )
}

@Composable
private fun StoryRow(name: String, chosen: Boolean, onPick: () -> Unit) {
    val p = palette
    Text(if (chosen) "✓ $name" else name, color = p.text, style = MaterialTheme.typography.bodyLarge,
        fontWeight = if (chosen) FontWeight.SemiBold else FontWeight.Normal,
        modifier = Modifier.fillMaxWidth().clip(RoundedCornerShape(8.dp))
            .clickable(onClickLabel = "Choose $name", role = Role.Button, onClick = onPick)
            .semantics { selected = chosen; contentDescription = name }
            .padding(horizontal = 8.dp, vertical = 10.dp))
}
