package com.turnhub.android.ui.tablet

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.IntrinsicSize
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.selection.selectable
import androidx.compose.foundation.selection.selectableGroup
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.clip
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.selection.toggleable
import androidx.compose.material3.FilterChip
import androidx.compose.material3.FilterChipDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.unit.dp
import com.turnhub.android.domain.StarCommander
import com.turnhub.android.domain.TurnTimerStatus
import com.turnhub.android.protocol.GameProfile
import com.turnhub.android.standalone.StandaloneGame
import com.turnhub.android.ui.components.AccentButton
import com.turnhub.android.ui.theme.palette

// Setup pieces both full-screen tables share, so the game is set up the same
// way with or without Atlas.

/** Shared turn timer durations. Timeout policy belongs to the game authority. */
@OptIn(ExperimentalLayoutApi::class)
@Composable
internal fun TurnTimerChips(selectedMs: Long, enabled: Boolean, onSelect: (Long) -> Unit) {
    val p = palette
    Text("Turn timer", color = p.muted, style = MaterialTheme.typography.labelLarge)
    FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
        StandaloneGame.TURN_TIMER_PRESETS_MS.forEach { ms ->
            FilterChip(
                selected = selectedMs == ms,
                onClick = { onSelect(ms) },
                label = { Text(TurnTimerStatus.settingLabel(ms)) },
                enabled = enabled,
                colors = FilterChipDefaults.filterChipColors(selectedContainerColor = p.accentSoft),
            )
        }
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
internal fun TimerRulesEditor(rules: com.turnhub.android.domain.LocalTimerRules, timerMs: Long,
    enabled: Boolean, onChange: (com.turnhub.android.domain.LocalTimerRules) -> Unit) {
    com.turnhub.android.ui.components.DisclosureCard("Timer rules") {
        Text("When time runs out", color = palette.text)
        FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            com.turnhub.android.domain.TimeoutAction.entries.forEach { action ->
                FilterChip(rules.timeout == action, { onChange(rules.copy(timeout = action)) },
                    label = { Text(action.label) }, enabled = enabled)
            }
        }
        Text(when (rules.timeout) {
            com.turnhub.android.domain.TimeoutAction.OVERTIME -> "Keep counting until the player passes."
            com.turnhub.android.domain.TimeoutAction.PASS -> "Pass to the next player at the limit."
            com.turnhub.android.domain.TimeoutAction.DEMERIT -> "Add one demerit, then keep counting."
            com.turnhub.android.domain.TimeoutAction.PASS_AND_DEMERIT -> "Add one demerit and pass the turn."
        }, color = palette.muted, style = MaterialTheme.typography.bodySmall)
        SetupSwitch("Earn extensions", null, rules.earnExtensions, enabled) { onChange(rules.copy(earnExtensions = it)) }
        if (rules.earnExtensions) {
            Text("Pass before", color = palette.muted)
            FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                listOf(75, 100).forEach { percent ->
                    FilterChip(rules.earnBeforePercent == percent, { onChange(rules.copy(earnBeforePercent = percent)) },
                        label = { Text("$percent% of the limit") }, enabled = enabled)
                }
            }
            Text("Maximum earned per game", color = palette.muted)
            FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                (1..3).forEach { count ->
                    FilterChip(rules.maxEarnedPerGame == count, { onChange(rules.copy(maxEarnedPerGame = count)) },
                        label = { Text("$count") }, enabled = enabled)
                }
            }
            Text("Earn from your fourth turn. Each adds ${com.turnhub.android.domain.TableClock.format(timerMs / 2)}. " +
                "Use one per turn, before time runs out. Extended turns don’t earn another.",
                color = palette.muted, style = MaterialTheme.typography.bodySmall)
        }
    }
}

/** A labelled switch row; the whole row toggles. */
@Composable
internal fun SetupSwitch(title: String, detail: String?, checked: Boolean, enabled: Boolean, onChange: (Boolean) -> Unit) {
    val p = palette
    Row(
        Modifier.fillMaxWidth().toggleable(value = checked, enabled = enabled, role = Role.Switch, onValueChange = onChange),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Column(Modifier.weight(1f)) {
            Text(title, color = p.text)
            detail?.let { Text(it, color = p.muted, style = MaterialTheme.typography.bodySmall) }
        }
        Switch(checked = checked, onCheckedChange = null, enabled = enabled)
    }
}

/**
 * Game modes beyond the standard game, for Magic and Commander: Two-Headed Giant,
 * and Star Commander (its own format, `mtg_star_commander`; choosing it turns
 * Two-Headed Giant off, and Standard from it goes back to Commander).
 */
enum class GameMode(val label: String, val detail: String) {
    // "Free-for-all": the Standard tile is the Magic format, so the mode needs its own word.
    STANDARD("Free-for-all", "Everyone for themselves."),
    TWO_HEADED_GIANT("Two-Headed Giant", "Players next to each other play as teams of two (1 and 2, 3 and 4, and so on), sharing life, poison and the turn."),
    STAR_COMMANDER("Star Commander", "Five players. Your neighbors are allies who can't deal you Commander damage; " +
        "win when both players across the star are out."),
    ;

    companion object {
        fun of(profile: GameProfile, twoHeadedGiant: Boolean): GameMode = when {
            profile.starCommander -> STAR_COMMANDER
            twoHeadedGiant -> TWO_HEADED_GIANT
            else -> STANDARD
        }

        /** Star Commander is a Commander variant: Standard Magic offers only free-for-all and Two-Headed Giant. */
        fun availableFor(profile: GameProfile): List<GameMode> =
            if (profile.commanderRules) entries else entries.filter { it != STAR_COMMANDER }
    }
}

/**
 * The lobby's format chips select a format's base; Star Commander shows as
 * Commander, and tapping Commander then keeps it (Standard leaves Star).
 */
internal fun formatSelected(current: GameProfile, chip: GameProfile): Boolean =
    current == chip || (current.starCommander && chip == GameProfile.MTG_COMMANDER)

@OptIn(ExperimentalLayoutApi::class)
@Composable
internal fun GameModeChips(profile: GameProfile, selected: GameMode, enabled: Boolean, onSelect: (GameMode) -> Unit) {
    val p = palette
    Text("Game mode", color = p.muted, style = MaterialTheme.typography.labelLarge)
    FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
        GameMode.availableFor(profile).forEach { mode ->
            FilterChip(
                selected = selected == mode,
                onClick = { onSelect(mode) },
                label = { Text(mode.label) },
                enabled = enabled,
                colors = FilterChipDefaults.filterChipColors(selectedContainerColor = p.accentSoft),
            )
        }
    }
    Text(selected.detail, color = p.faint, style = MaterialTheme.typography.bodySmall)
}

/**
 * The one big start button; a team game needs an even table of at least four, Star Commander
 * exactly five, and an official mode with [exactPlayers] (two teams of two, a Duel) that many.
 */
@Composable
internal fun StartTableButton(players: Int, teams: Boolean, enabled: Boolean, star: Boolean = false,
    exactPlayers: Int? = null, onStart: () -> Unit) {
    val ready = when {
        star -> players == StarCommander.PLAYERS
        exactPlayers != null -> players == exactPlayers
        else -> players >= 2 && (!teams || (players >= 4 && players % 2 == 0))
    }
    AccentButton(
        when {
            star && !ready -> "Star Commander needs exactly five players."
            exactPlayers != null && !ready && teams -> "Two-Headed Giant is two teams of two: add exactly four players"
            exactPlayers != null && !ready -> "This game is for exactly $exactPlayers players"
            players < 2 -> "Add two players to start"
            !ready -> "Two-Headed Giant needs an even table of four or more"
            else -> "Start full-screen game"
        },
        onStart,
        Modifier.fillMaxWidth().heightIn(min = 64.dp),
        enabled = enabled && ready,
    )
}

/** How the seat map is explained in both lobbies. */
internal const val SEAT_MAP_HINT = "This is how the table will look. Drag a player onto another seat to swap them, " +
    "or tap two seats. Turns go clockwise in number order."

/** The seat map's explanation in Two-Headed Giant. */
internal const val TEAM_MAP_HINT = "Each outlined panel is a team: players 1 and 2, then 3 and 4, and so on. " +
    "Teammates share one panel, life total and turn. Drag a player onto another seat to swap them, or tap two seats."

/** A game family in the picker: a heading, and a colour stripe on its tiles that the heading always names too. */
internal enum class GameFamily(val title: String) {
    MAGIC("Magic: The Gathering"),
    CARD_GAMES("More card games"),
    BOARD_GAMES("Board games"),
    CUSTOM("Your own game"),
}

/** One game in the picker; [detail] says what the table counts and who plays. */
internal data class GameChoice(val profile: GameProfile, val label: String, val detail: String, val family: GameFamily)

/** Formats offered in the shared lobby, by family; Atlas runs those that are [GameProfile.atlasSupported]. */
internal val FORMATS = listOf(
    GameChoice(GameProfile.MTG_COMMANDER, "Commander", "40 life · 2+ players", GameFamily.MAGIC),
    GameChoice(GameProfile.MTG, "Standard", "20 life · 2+ players", GameFamily.MAGIC),
    GameChoice(GameProfile.YUGIOH, "Yu-Gi-Oh!", "8000 Life Points · 2 players", GameFamily.CARD_GAMES),
    GameChoice(GameProfile.POKEMON, "Pokémon", "6 Prize cards · 2 players", GameFamily.CARD_GAMES),
    GameChoice(GameProfile.LORCANA, "Lorcana", "Race to 20 lore · 2+ players", GameFamily.CARD_GAMES),
    GameChoice(GameProfile.DEATH_MAY_DIE, "Death May Die", "Co-op · turns each round · investigators", GameFamily.BOARD_GAMES),
    GameChoice(GameProfile.GENERIC, "Other", "Any life total and player count", GameFamily.CUSTOM),
)

/**
 * The lobby's game picker: one section per family, each game a tile that says
 * what the table counts. [onSelect] gets the chosen tile's format; Star
 * Commander shows as Commander ([formatSelected]).
 */
@OptIn(ExperimentalLayoutApi::class)
@Composable
internal fun GamePicker(selected: GameProfile, enabled: Boolean, phoneHosted: Boolean, onSelect: (GameProfile) -> Unit) {
    val p = palette
    Column(Modifier.fillMaxWidth().selectableGroup(), verticalArrangement = Arrangement.spacedBy(12.dp)) {
        FORMATS.filter { phoneHosted || it.profile.atlasSupported }.groupBy { it.family }.forEach { (family, choices) ->
            Column(verticalArrangement = Arrangement.spacedBy(6.dp)) {
                Text(family.title, color = p.muted, style = MaterialTheme.typography.labelLarge,
                    modifier = Modifier.semantics { heading() })
                FlowRow(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp),
                    verticalArrangement = Arrangement.spacedBy(8.dp), maxItemsInEachRow = 2) {
                    choices.forEach { choice ->
                        val on = formatSelected(selected, choice.profile)
                        GameTile(choice, on, enabled, Modifier.weight(1f)) { if (!on) onSelect(choice.profile) }
                    }
                }
            }
        }
    }
}

@Composable
private fun GameTile(choice: GameChoice, selected: Boolean, enabled: Boolean, modifier: Modifier, onClick: () -> Unit) {
    val p = palette
    val shape = RoundedCornerShape(14.dp)
    val stripe = when (choice.family) {
        GameFamily.MAGIC -> p.accent
        GameFamily.CARD_GAMES -> p.info
        GameFamily.BOARD_GAMES -> p.good
        GameFamily.CUSTOM -> p.faint
    }
    Row(
        modifier
            .heightIn(min = 64.dp)
            .clip(shape)
            .background(if (selected) p.accentSoft else p.surface)
            .border(if (selected) 2.dp else 1.dp, if (selected) p.accent else p.line, shape)
            .selectable(selected, enabled = enabled, role = Role.RadioButton, onClick = onClick)
            .alpha(if (enabled) 1f else .5f)
            .height(IntrinsicSize.Min),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Box(Modifier.width(6.dp).fillMaxHeight().background(stripe))
        Column(Modifier.weight(1f).padding(horizontal = 12.dp, vertical = 10.dp)) {
            Text(choice.label, color = p.text, style = MaterialTheme.typography.titleMedium, fontWeight = FontWeight.SemiBold)
            Text(choice.detail, color = p.muted, style = MaterialTheme.typography.bodySmall)
        }
        if (selected) Text("✓", color = p.accent, style = MaterialTheme.typography.titleMedium,
            fontWeight = FontWeight.Bold, modifier = Modifier.padding(end = 12.dp))
    }
}
