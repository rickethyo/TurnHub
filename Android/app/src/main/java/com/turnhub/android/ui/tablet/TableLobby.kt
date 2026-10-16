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
import androidx.compose.ui.text.font.FontStyle
import androidx.compose.ui.unit.dp
import com.turnhub.android.domain.ControllerHandle
import com.turnhub.android.domain.StarCommander
import com.turnhub.android.domain.TablePlayer
import com.turnhub.android.domain.TableSummary
import com.turnhub.android.protocol.GameProfile
import com.turnhub.android.standalone.StandaloneGame
import com.turnhub.android.standalone.officialRules
import com.turnhub.android.ui.components.BrassCard
import com.turnhub.android.ui.components.AppVersionLabel
import com.turnhub.android.ui.components.Eyebrow
import com.turnhub.android.ui.components.DisclosureCard
import com.turnhub.android.ui.components.ToneButton
import com.turnhub.android.ui.theme.palette

internal data class LobbySavedPlayer(val id: String, val name: String, val displayId: String = id)

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
    /** Picks who goes first; null hides the choice. */
    val chooseStarter: ((TablePlayer) -> Unit)? = null,
    /** House rules for official modes; null hides the choice. */
    val houseRules: ((Boolean) -> Unit)? = null,
    val timerRules: ((com.turnhub.android.domain.LocalTimerRules) -> Unit)? = null,
    /** Names a player's commander in Commander modes (optional); null hides it. */
    val commander: ((TablePlayer, String?, String?) -> Unit)? = null,
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
    /** Who goes first when nobody is chosen, in this table's words. */
    starterHint: String = "Tap First? to choose who goes first.",
    headerActions: @Composable FlowRowScope.() -> Unit = {},
    feedback: @Composable () -> Unit = {},
    footer: @Composable ColumnScope.() -> Unit = {},
    embedded: Boolean = false,
    /** Extra game options under the standard ones (the device game's Planechase switch). */
    extraSettings: @Composable () -> Unit = {},
    /** The phone runs this game: it also offers the formats Atlas doesn't run yet (Pokémon). */
    phoneHosted: Boolean = false,
) {
    val p = palette
    val players = summary.players.sortedBy { it.playerNumber }
    val settings = summary.settings
    // Official modes keep the official values unless house rules are on.
    val officialMode = officialRules(settings.profile, settings.twoHeadedGiant)
    val official = officialMode.takeIf { actions.houseRules != null }
    val locked = official != null && !settings.houseRules
    Box(Modifier.fillMaxSize().then(if (embedded) Modifier else Modifier.safeDrawingPadding()), contentAlignment = Alignment.TopCenter) {
        Column(Modifier.widthIn(max = 640.dp).fillMaxWidth().verticalScroll(rememberScrollState()).padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(14.dp)) {
            if (!embedded) Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
                Text("Full-screen table", color = p.text, style = MaterialTheme.typography.headlineSmall,
                    modifier = Modifier.semantics { heading() })
                AppVersionLabel()
                FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) { headerActions() }
            }
            introduction?.let { Text(it, color = p.muted, style = MaterialTheme.typography.bodySmall, fontStyle = FontStyle.Italic) }
            feedback()
            BrassCard {
                Eyebrow("At the table") {
                    TextButton(onClick = actions.clear, enabled = enabled && players.any(actions.canRemove)) { Text("Clear all") }
                }
                // The preview takes the mode's shape from the first player: the star's five
                // seats, or Two-Headed Giant's split team panels, filling in as players join.
                if (settings.profile.starCommander && players.size <= StarCommander.PLAYERS) {
                    val seats = summary.starSeats()
                    StarMap(seats, onSwap = { a, b -> if (enabled) actions.swap(a, b) })
                    StarRelationsList(seats)
                    val starLayout = rememberStarLayout()
                    SetupSwitch("Star layout at the table", STAR_LAYOUT_HINT, starLayout.on, true, starLayout.set)
                } else {
                    SeatMap(players.map { it.label }, { a, b -> if (enabled) actions.swap(a, b) },
                        backgrounds = players.map { it.tileBackground }, colors = players.map { it.color },
                        teams = settings.twoHeadedGiant)
                }
                // How this mode's seating works, right under the map it explains.
                Text(when {
                    settings.profile.starCommander -> STAR_MAP_HINT
                    settings.twoHeadedGiant -> TEAM_MAP_HINT
                    else -> SEAT_MAP_HINT
                }, color = p.muted, style = MaterialTheme.typography.bodySmall)
                var namingCommander by rememberSaveable { mutableStateOf<Int?>(null) }
                players.firstOrNull { it.playerNumber == namingCommander }?.let { player ->
                    actions.commander?.let { save -> CommanderPickerDialog(player, { name, art -> save(player, name, art); namingCommander = null }) { namingCommander = null } }
                }
                players.forEach { player ->
                    Column(Modifier.fillMaxWidth(), verticalArrangement = Arrangement.spacedBy(4.dp)) {
                        Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                            val hue = com.turnhub.android.ui.components.playerColor(player.color, p.avatarColor(player.playerNumber))
                            Box(Modifier.size(30.dp).background(hue, CircleShape), contentAlignment = Alignment.Center) {
                                Text("${player.playerNumber}", color = com.turnhub.android.ui.components.colorInk(hue), style = MaterialTheme.typography.labelLarge)
                            }
                            Column(Modifier.weight(1f)) {
                                Text(player.label, color = p.text, style = MaterialTheme.typography.bodyLarge)
                                player.commander?.let { Text(it, color = p.muted, style = MaterialTheme.typography.bodySmall, fontStyle = FontStyle.Italic) }
                                rememberCommanderArt(player.commander, player.commanderArt)?.let { art ->
                                    Text("${art.cardName} · Art: ${art.artist} (Scryfall)", color = p.faint, style = MaterialTheme.typography.labelSmall)
                                }
                            }
                            if (settings.twoHeadedGiant) Text("Team ${(player.playerNumber + 1) / 2}", color = p.muted,
                                style = MaterialTheme.typography.labelMedium)
                            if (player.controller.kind == ControllerHandle.Kind.PHYSICAL) Text("Sigil", color = p.muted,
                                style = MaterialTheme.typography.labelMedium)
                        }
                        FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
                            actions.chooseStarter?.let { choose ->
                                val first = summary.starterPlayerNumber == player.playerNumber
                                FilterChip(selected = first, onClick = { choose(player) }, enabled = enabled,
                                    label = { Text(if (first) "Goes first" else "First?") },
                                    colors = FilterChipDefaults.filterChipColors(selectedContainerColor = p.accentSoft),
                                    modifier = Modifier.semantics {
                                        contentDescription = if (first) "${player.label} goes first. Tap to clear" else "Make ${player.label} go first"
                                    })
                            }
                            if (settings.profile.commanderRules && actions.commander != null) {
                                TextButton(onClick = { namingCommander = player.playerNumber }, enabled = enabled,
                                    modifier = Modifier.semantics { contentDescription = "Name ${player.label}'s commander and pick its card art" }) {
                                    Text(if (player.commander == null) "Name commander" else "Change commander")
                                }
                            }
                            actions.background?.takeIf { actions.canEditBackground(player) }?.let { edit ->
                                TextButton(onClick = { edit(player) }, enabled = enabled,
                                    modifier = Modifier.semantics { contentDescription = "Choose the color or photo behind ${player.label}'s tile" }) { Text("Tile color & photo") }
                            }
                            if (actions.canRemove(player)) IconButton(onClick = { actions.remove(player) }, enabled = enabled,
                                modifier = Modifier.semantics { contentDescription = "Remove ${player.label}" }) {
                                Text("×", color = p.muted, style = MaterialTheme.typography.titleLarge)
                            }
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
                    Text(identityHint, color = p.faint, style = MaterialTheme.typography.bodySmall)
                    FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
                        savedPlayers.forEach { player ->
                            SuggestionChip(onClick = { actions.select(player) }, label = {
                                Column {
                                    Text(player.name.ifBlank { "Unnamed" })
                                    Text("ID ${player.displayId}", style = MaterialTheme.typography.bodySmall)
                                }
                            },
                                enabled = enabled && canAdd)
                        }
                    }
                }
                if (actions.chooseStarter != null && players.size >= 2) Text(
                    summary.players.firstOrNull { it.playerNumber == summary.starterPlayerNumber }?.let {
                        "${it.label}${if (settings.twoHeadedGiant) "'s team" else ""} goes first."
                    } ?: starterHint, color = p.muted, style = MaterialTheme.typography.bodySmall)
            }
            BrassCard {
                Eyebrow("Game")
                if (!settingsEnabled) Text("Add a player to choose the game.", color = p.muted)
                FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    FORMATS.filter { phoneHosted || it.first.atlasSupported }.forEach { (profile, label, _) ->
                        FilterChip(selected = formatSelected(settings.profile, profile), onClick = {
                            if (!formatSelected(settings.profile, profile)) actions.settings(profile,
                                StandaloneGame.defaultLife(profile, settings.twoHeadedGiant && profile.magicRules), null)
                        }, label = { Text(label) }, enabled = settingsEnabled,
                            colors = FilterChipDefaults.filterChipColors(selectedContainerColor = p.accentSoft))
                    }
                }
                if (settings.profile.magicRules) {
                    GameModeChips(GameMode.of(settings.profile, settings.twoHeadedGiant), settingsEnabled) { mode ->
                        if (mode != GameMode.of(settings.profile, settings.twoHeadedGiant)) {
                            val change = settingsForMode(settings.profile, mode)
                            actions.settings(change.profile, change.life, change.teams)
                        }
                    }
                }
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Text(when {
                        officialMode == null -> "Custom mode"
                        locked || official == null -> "Official ${officialMode.name} rules"
                        else -> "House rules"
                    }, color = p.muted, style = MaterialTheme.typography.labelLarge, modifier = Modifier.weight(1f))
                    ModeRulesButton(settings.profile, settings.twoHeadedGiant)
                }
                var life by rememberSaveable(settings.startingLife) { mutableStateOf("${settings.startingLife}") }
                Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    OutlinedTextField(life, { life = it.filter(Char::isDigit).take(5) },
                        label = { Text(when {
                            settings.profile.pokemon -> "Prize cards"
                            settings.twoHeadedGiant -> "Starting life per team"
                            else -> "Starting life"
                        }) },
                        singleLine = true, enabled = settingsEnabled && !locked, keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number),
                        modifier = Modifier.weight(1f))
                    ToneButton("Set", { life.toIntOrNull()?.let { actions.settings(null, it, null) } },
                        enabled = settingsEnabled && !locked && (life.toIntOrNull() ?: 0) > 0 && life != "${settings.startingLife}")
                }
                if (locked) Text(if (settings.profile.pokemon) "Official 6 Prize cards. Turn on house rules to change them."
                    else "Official starting life. Turn on house rules to change it.", color = p.faint,
                    style = MaterialTheme.typography.bodySmall)
                if (official != null) actions.houseRules?.let { setHouseRules ->
                    SetupSwitch("House rules", (if (settings.profile.pokemon) "Change the Prize cards and player count. "
                        else "Change the starting life, poison limit and player count. ") +
                        "The game no longer follows the official ${official.name} rules.", settings.houseRules, settingsEnabled, setHouseRules)
                }
                TurnTimerChips(settings.turnTimerMs, settingsEnabled, actions.timer)
                if (settings.turnTimerEnabled) {
                    val rules = summary.localTimerRules
                    val changeRules = actions.timerRules
                    if (rules != null && changeRules != null) TimerRulesEditor(rules, settings.turnTimerMs, settingsEnabled, changeRules)
                    else Text("At the limit, Atlas keeps counting overtime.", color = p.muted, style = MaterialTheme.typography.bodySmall)
                }
                // Pokémon has no life: taking the last Prize card wins.
                if (!settings.profile.pokemon) {
                    SetupSwitch("Automatic elimination at zero life", null, settings.autoEliminateAtZeroLife, settingsEnabled, actions.zeroLife)
                }
                extraSettings()
            }
            StartTableButton(players.size, settings.twoHeadedGiant, enabled, settings.profile.starCommander,
                official?.players?.takeIf { locked }, actions.start)
            // In the app, sounds and bug reports live in Settings and the banner; the
            // full-screen lobby has neither, so it keeps them at the bottom.
            if (!embedded) {
                DisclosureCard("Game sounds") { com.turnhub.android.ui.components.GameSoundControls() }
                DisclosureCard("Feedback") {
                    com.turnhub.android.ui.components.ReportBugButton(
                        com.turnhub.android.ui.components.tableBugReportContext(summary, "Table setup"))
                }
            }
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
