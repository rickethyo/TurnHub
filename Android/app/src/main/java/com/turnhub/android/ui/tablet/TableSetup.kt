package com.turnhub.android.ui.tablet

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

/** The turn timer presets: Off, 1, 2, 3 and 5 minutes (GAMEPLAY.md). */
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
    Text("Running out never passes the turn: the clock counts the overtime until the player passes.",
        color = p.faint, style = MaterialTheme.typography.bodySmall)
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
    STANDARD("Standard", "Everyone for themselves."),
    TWO_HEADED_GIANT("Two-Headed Giant", "Neighbours in turn order play as teams of two, sharing life, poison and the turn."),
    STAR_COMMANDER("Star Commander", "Five players. Your neighbours are allies who can't deal you Commander damage; " +
        "win when both players across the star are out."),
    ;

    companion object {
        fun of(profile: GameProfile, twoHeadedGiant: Boolean): GameMode = when {
            profile.starCommander -> STAR_COMMANDER
            twoHeadedGiant -> TWO_HEADED_GIANT
            else -> STANDARD
        }
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
internal fun GameModeChips(selected: GameMode, enabled: Boolean, onSelect: (GameMode) -> Unit) {
    val p = palette
    Text("Game mode", color = p.muted, style = MaterialTheme.typography.labelLarge)
    FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
        GameMode.entries.forEach { mode ->
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

/** The one big start button; a team game needs an even table of at least four, Star Commander exactly five. */
@Composable
internal fun StartTableButton(players: Int, teams: Boolean, enabled: Boolean, star: Boolean = false, onStart: () -> Unit) {
    val ready = if (star) players == StarCommander.PLAYERS else players >= 2 && (!teams || (players >= 4 && players % 2 == 0))
    AccentButton(
        when {
            star && !ready -> "Star Commander needs exactly five players."
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


internal val FORMATS = listOf(
    Triple(GameProfile.MTG_COMMANDER, "Commander", 40),
    Triple(GameProfile.MTG, "Magic", 20),
    Triple(GameProfile.YUGIOH, "Yu-Gi-Oh!", 8000),
    Triple(GameProfile.GENERIC, "Other", 20),
)
