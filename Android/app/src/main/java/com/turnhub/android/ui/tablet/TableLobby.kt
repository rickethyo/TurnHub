package com.turnhub.android.ui.tablet

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.unit.dp
import com.turnhub.android.domain.ControllerHandle
import com.turnhub.android.domain.TablePlayer
import com.turnhub.android.domain.TableSummary
import com.turnhub.android.protocol.GameProfile
import com.turnhub.android.standalone.StandaloneGame
import com.turnhub.android.ui.components.BrassCard
import com.turnhub.android.ui.components.Eyebrow
import com.turnhub.android.ui.components.ToneButton
import com.turnhub.android.ui.theme.palette

internal data class LobbySavedPlayer(val id: String, val name: String)

/** The two authorities supply actions; all setup layout and input behavior lives here. */
internal data class TableLobbyActions(
    val add: (String) -> Unit,
    val select: (LobbySavedPlayer) -> Unit,
    val remove: (TablePlayer) -> Unit,
    val canRemove: (TablePlayer) -> Boolean,
    val clear: () -> Unit,
    val swap: (Int, Int) -> Unit,
    val settings: (GameProfile?, Int?, Boolean?) -> Unit,
    val timer: (Long) -> Unit,
    val zeroLife: (Boolean) -> Unit,
    val start: () -> Unit,
    val background: ((TablePlayer) -> Unit)? = null,
    val canEditBackground: (TablePlayer) -> Boolean = { true },
)

@OptIn(ExperimentalLayoutApi::class)
@Composable
internal fun TableLobby(
    summary: TableSummary,
    actions: TableLobbyActions,
    savedPlayers: List<LobbySavedPlayer>,
    enabled: Boolean,
    settingsEnabled: Boolean,
    canAdd: Boolean,
    identityHint: String,
    savedTitle: String,
    introduction: String? = null,
    headerActions: @Composable RowScope.() -> Unit = {},
    feedback: @Composable () -> Unit = {},
    footer: @Composable ColumnScope.() -> Unit = {},
) {
    val p = palette
    val players = summary.players.sortedBy { it.playerNumber }
    val settings = summary.settings
    Box(Modifier.fillMaxSize().safeDrawingPadding(), contentAlignment = Alignment.TopCenter) {
        Column(Modifier.widthIn(max = 640.dp).fillMaxWidth().verticalScroll(rememberScrollState()).padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(14.dp)) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text("Full-screen table", color = p.text, style = MaterialTheme.typography.headlineSmall,
                    modifier = Modifier.weight(1f).semantics { heading() })
                headerActions()
            }
            introduction?.let { Text(it, color = p.muted) }
            feedback()
            BrassCard {
                Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.SpaceBetween) {
                    Eyebrow("At the table")
                    TextButton(onClick = actions.clear, enabled = enabled && players.any(actions.canRemove)) { Text("Clear all") }
                }
                if (summary.star) {
                    val seats = summary.starSeats()
                    StarMap(seats, onSwap = { a, b -> if (enabled) actions.swap(a, b) })
                    StarRelationsList(seats)
                    Text(STAR_MAP_HINT, color = p.faint, style = MaterialTheme.typography.bodySmall)
                } else {
                    SeatMap(players.map { it.label }, { a, b -> if (enabled) actions.swap(a, b) },
                        backgrounds = players.map { it.tileBackground }, colors = players.map { it.color })
                    Text(SEAT_MAP_HINT, color = p.faint, style = MaterialTheme.typography.bodySmall)
                }
                players.forEach { player ->
                    Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                        Box(Modifier.size(30.dp).background(p.avatarColor(player.playerNumber), CircleShape), contentAlignment = Alignment.Center) {
                            Text("${player.playerNumber}", color = p.avatarText, style = MaterialTheme.typography.labelLarge)
                        }
                        Text(player.label, color = p.text, style = MaterialTheme.typography.bodyLarge, modifier = Modifier.weight(1f))
                        if (player.controller.kind == ControllerHandle.Kind.PHYSICAL) Text("Sigil", color = p.muted,
                            style = MaterialTheme.typography.labelMedium)
                        actions.background?.takeIf { actions.canEditBackground(player) }?.let { edit ->
                            TextButton(onClick = { edit(player) }, enabled = enabled,
                                modifier = Modifier.semantics { contentDescription = "Tile background for ${player.label}" }) { Text("Background") }
                        }
                        if (actions.canRemove(player)) IconButton(onClick = { actions.remove(player) }, enabled = enabled,
                            modifier = Modifier.semantics { contentDescription = "Remove ${player.label}" }) {
                            Text("×", color = p.muted, style = MaterialTheme.typography.titleLarge)
                        }
                    }
                }
                var name by rememberSaveable { mutableStateOf("") }
                val add = {
                    if (enabled && canAdd && name.isNotBlank()) { actions.add(name.trim()); name = "" }
                }
                Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    OutlinedTextField(name, { name = it.take(StandaloneGame.MAX_NAME) }, label = { Text("Player's name") },
                        singleLine = true, enabled = enabled && canAdd,
                        keyboardOptions = KeyboardOptions(imeAction = ImeAction.Done), keyboardActions = KeyboardActions(onDone = { add() }),
                        modifier = Modifier.weight(1f))
                    ToneButton("Add", add, enabled = enabled && canAdd && name.isNotBlank())
                }
                if (savedPlayers.isNotEmpty()) {
                    Text(savedTitle, color = p.muted, style = MaterialTheme.typography.labelLarge)
                    FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
                        savedPlayers.forEach { player ->
                            SuggestionChip(onClick = { actions.select(player) }, label = { Text(player.name.ifBlank { "Unnamed" }) },
                                enabled = enabled && canAdd)
                        }
                    }
                }
                Text(identityHint, color = p.faint, style = MaterialTheme.typography.bodySmall)
            }
            BrassCard {
                Eyebrow("Game")
                if (!settingsEnabled) Text("Add a player to choose the game.", color = p.muted)
                FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    FORMATS.forEach { (profile, label, _) ->
                        FilterChip(selected = formatSelected(settings.profile, profile), onClick = {
                            if (!formatSelected(settings.profile, profile)) actions.settings(profile,
                                StandaloneGame.defaultLife(profile, settings.twoHeadedGiant && profile.magicRules), null)
                        }, label = { Text(label) }, enabled = settingsEnabled,
                            colors = FilterChipDefaults.filterChipColors(selectedContainerColor = p.accentSoft))
                    }
                }
                var life by rememberSaveable(settings.startingLife) { mutableStateOf("${settings.startingLife}") }
                Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    OutlinedTextField(life, { life = it.filter(Char::isDigit).take(5) },
                        label = { Text(if (settings.twoHeadedGiant) "Starting life per team" else "Starting life") },
                        singleLine = true, enabled = settingsEnabled, keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number),
                        modifier = Modifier.weight(1f))
                    ToneButton("Set", { life.toIntOrNull()?.let { actions.settings(null, it, null) } },
                        enabled = settingsEnabled && (life.toIntOrNull() ?: 0) > 0 && life != "${settings.startingLife}")
                }
                if (settings.profile.magicRules) {
                    GameModeChips(GameMode.of(settings.profile, settings.twoHeadedGiant), settingsEnabled) { mode ->
                        if (mode != GameMode.of(settings.profile, settings.twoHeadedGiant)) {
                            val change = settingsForMode(settings.profile, mode)
                            actions.settings(change.profile, change.life, change.teams)
                        }
                    }
                }
                TurnTimerChips(settings.turnTimerMs, settingsEnabled, actions.timer)
                SetupSwitch("Automatic elimination at zero life", null, settings.autoEliminateAtZeroLife, settingsEnabled, actions.zeroLife)
            }
            StartTableButton(players.size, settings.twoHeadedGiant, enabled, settings.profile.starCommander, actions.start)
            footer()
        }
    }
}

internal data class LobbyModeSettings(val profile: GameProfile?, val life: Int?, val teams: Boolean)

/** Both authorities receive the same format/life/team request for a mode change. */
internal fun settingsForMode(current: GameProfile, mode: GameMode): LobbyModeSettings {
    val profile = when {
        mode == GameMode.STAR_COMMANDER -> GameProfile.MTG_STAR_COMMANDER
        current.starCommander -> GameProfile.MTG_COMMANDER
        else -> null
    }
    val teams = mode == GameMode.TWO_HEADED_GIANT
    return LobbyModeSettings(profile, StandaloneGame.defaultLife(profile ?: current, teams), teams)
}
