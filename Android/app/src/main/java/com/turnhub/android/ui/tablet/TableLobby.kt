package com.turnhub.android.ui.tablet

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.heightIn
import androidx.compose.ui.draw.clip
import androidx.compose.foundation.clickable
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
import com.turnhub.android.domain.InvestigatorCard
import com.turnhub.android.domain.InvestigatorCardSlot
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
    /**
     * Sets who goes first (null: random) and starts. When given, Start asks who goes first
     * full screen and the setup rows carry no First chips.
     */
    val startWith: ((TablePlayer?) -> Unit)? = null,
    /** House rules for official modes; null hides the choice. */
    val houseRules: ((Boolean) -> Unit)? = null,
    val timerRules: ((com.turnhub.android.domain.LocalTimerRules) -> Unit)? = null,
    /**
     * Picks a player's tile card in Magic formats, named as their commander in Commander modes
     * unless `cardOnly` (optional); null hides it.
     */
    val commander: ((player: TablePlayer, name: String?, printing: String?, cardOnly: Boolean) -> Unit)? = null,
    /** Death May Die: names (or clears, null) a player's investigator; null hides it. */
    val investigator: ((player: TablePlayer, name: String?) -> Unit)? = null,
    val dealInvestigatorCards: ((TablePlayer, InvestigatorCardSlot) -> Unit)? = null,
    val investigatorCard: ((TablePlayer, InvestigatorCardSlot, InvestigatorCard?) -> Unit)? = null,
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
    starterHint: String = "Tap First to choose who goes first.",
    headerActions: @Composable FlowRowScope.() -> Unit = {},
    feedback: @Composable () -> Unit = {},
    footer: @Composable ColumnScope.() -> Unit = {},
    embedded: Boolean = false,
    /** Extra game options under the standard ones (the device game's Planechase switch). */
    extraSettings: @Composable () -> Unit = {},
    /** The phone runs this game: it also offers the formats Atlas doesn't run yet (Pokémon). */
    phoneHosted: Boolean = false,
    startReady: Boolean = true,
    startHint: String? = null,
) {
    val p = palette
    val players = summary.players.sortedBy { it.playerNumber }
    val settings = summary.settings
    // Official modes keep the official values unless house rules are on.
    val officialMode = officialRules(settings.profile, settings.twoHeadedGiant)
    val official = officialMode.takeIf { actions.houseRules != null }
    val locked = official != null && !settings.houseRules
    var guided by rememberSaveable { mutableStateOf(false) }
    Box(Modifier.fillMaxSize().then(if (embedded) Modifier else Modifier.safeDrawingPadding()), contentAlignment = Alignment.TopCenter) {
        Column(Modifier.widthIn(max = 880.dp).fillMaxWidth().verticalScroll(rememberScrollState()).padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(14.dp)) {
            if (!embedded) Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
                Text("Full-screen table", color = p.text, style = MaterialTheme.typography.headlineSmall,
                    modifier = Modifier.semantics { heading() })
                AppVersionLabel()
                FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) { headerActions() }
            }
            // The app's Game tab opens straight on the table; the tagline is for the full-screen lobby.
            introduction?.takeIf { !embedded }?.let { Text(it, color = p.muted, style = MaterialTheme.typography.bodySmall, fontStyle = FontStyle.Italic) }
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
                } else if (settings.profile.cooperative) {
                    ListSeatMap(players.map { it.label }, players.map { it.investigator }, { a, b -> if (enabled) actions.swap(a, b) },
                        backgrounds = players.map { it.tileBackground }, colors = players.map { it.color })
                } else {
                    SeatMap(players.map { it.label }, { a, b -> if (enabled) actions.swap(a, b) },
                        backgrounds = players.map { it.tileBackground }, colors = players.map { it.color },
                        teams = settings.twoHeadedGiant, cards = players.map { it.commander to it.commanderArt })
                }
                // How this mode's seating works, right under the map it explains.
                Text(when {
                    settings.profile.starCommander -> STAR_MAP_HINT
                    settings.profile.cooperative -> COOP_MAP_HINT
                    settings.twoHeadedGiant -> TEAM_MAP_HINT
                    else -> SEAT_MAP_HINT
                }, color = p.muted, style = MaterialTheme.typography.bodySmall)
                var namingCommander by rememberSaveable { mutableStateOf<Int?>(null) }
                players.firstOrNull { it.playerNumber == namingCommander }?.let { player ->
                    actions.commander?.let { save -> CommanderPickerDialog(player, settings.profile.commanderRules, com.turnhub.android.data.TileCardGame.forProfile(settings.profile),
                        { name, art, cardOnly -> save(player, name, art, cardOnly); namingCommander = null }) { namingCommander = null } }
                }
                var choosingInvestigator by rememberSaveable { mutableStateOf<Int?>(null) }
                players.firstOrNull { it.playerNumber == choosingInvestigator }?.let { player ->
                    actions.investigator?.let { save ->
                        val taken = players.filter { it.playerNumber != player.playerNumber }
                            .mapNotNull { other -> other.investigator?.let { it to other.label } }.toMap()
                        InvestigatorPickerDialog(player, taken, { name -> save(player, name); choosingInvestigator = null }) { choosingInvestigator = null }
                    }
                }
                val investigators = settings.profile.deathMayDie && actions.investigator != null
                players.forEach { player ->
                    Column(Modifier.fillMaxWidth(), verticalArrangement = Arrangement.spacedBy(4.dp)) {
                        Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                            val hue = com.turnhub.android.ui.components.playerColor(player.color, p.avatarColor(player.playerNumber))
                            Box(Modifier.size(30.dp).background(hue, CircleShape), contentAlignment = Alignment.Center) {
                                Text("${player.playerNumber}", color = com.turnhub.android.ui.components.colorInk(hue), style = MaterialTheme.typography.labelLarge)
                            }
                            Column(Modifier.weight(1f)) {
                                Text(player.label, color = p.text, style = MaterialTheme.typography.bodyLarge, maxLines = 1,
                                    overflow = androidx.compose.ui.text.style.TextOverflow.Ellipsis)
                                // The usual next step for an empty seat is one tap away, outside the menu.
                                if (investigators) {
                                    if (player.investigator != null) Text(player.investigator, color = p.muted,
                                        style = MaterialTheme.typography.bodySmall, fontStyle = FontStyle.Italic)
                                    else SeatShortcut("Choose investigator", "Choose ${player.label}'s investigator", enabled) {
                                        choosingInvestigator = player.playerNumber
                                    }
                                }
                                if (actions.commander != null && settings.profile.commanderRules && player.commander == null) {
                                    SeatShortcut("Name commander", "Name ${player.label}'s commander", enabled) {
                                        namingCommander = player.playerNumber
                                    }
                                }
                                player.namedCommander?.let { Text(it, color = p.muted, style = MaterialTheme.typography.bodySmall, fontStyle = FontStyle.Italic) }
                                // The tile card shows as soon as it is saved; its credit follows once the art arrives.
                                val art = rememberCommanderArt(player.commander, player.commanderArt)
                                player.commander?.let { card ->
                                    Text(listOfNotNull(art?.cardName ?: card, art?.let { "Art: ${it.artist} (${it.source})" }).joinToString(" · "),
                                        color = p.faint, style = MaterialTheme.typography.labelSmall, maxLines = 1,
                                        overflow = androidx.compose.ui.text.style.TextOverflow.Ellipsis)
                                }
                            }
                            if (settings.twoHeadedGiant) Text("Team ${(player.playerNumber + 1) / 2}", color = p.muted,
                                style = MaterialTheme.typography.labelMedium)
                            if (player.controller.kind == ControllerHandle.Kind.PHYSICAL) Text("Sigil", color = p.muted,
                                style = MaterialTheme.typography.labelMedium)
                            // One row per player: the label never changes width, and the rest sits in one menu,
                            // so choosing who goes first or a card never reflows the list.
                            actions.chooseStarter?.takeIf { actions.startWith == null }?.let { choose ->
                                val first = summary.starterPlayerNumber == player.playerNumber
                                FilterChip(selected = first, onClick = { choose(player) }, enabled = enabled,
                                    label = { Text("First") },
                                    colors = FilterChipDefaults.filterChipColors(selectedContainerColor = p.accentSoft),
                                    modifier = Modifier.semantics {
                                        contentDescription = if (first) "${player.label} goes first. Tap to clear" else "Make ${player.label} go first"
                                    })
                            }
                            val items = buildList<Pair<String, () -> Unit>> {
                                if (investigators) add((if (player.investigator == null) "Choose investigator" else "Change investigator") to
                                    { choosingInvestigator = player.playerNumber })
                                if (actions.commander != null) add(when {
                                    player.commander == null -> if (settings.profile.commanderRules) "Name commander" else "Choose tile card"
                                    player.namedCommander != null -> "Change commander"
                                    else -> "Change tile card"
                                } to { namingCommander = player.playerNumber })
                                actions.background?.takeIf { actions.canEditBackground(player) }?.let { edit ->
                                    add("Tile color & photo" to { edit(player) })
                                }
                                if (actions.canRemove(player)) add("Remove ${player.label}" to { actions.remove(player) })
                            }
                            var menu by remember { mutableStateOf(false) }
                            Box {
                                IconButton(onClick = { menu = true }, enabled = enabled && items.isNotEmpty(),
                                    modifier = Modifier.semantics { contentDescription = "Options for ${player.label}" }) {
                                    Text("⋯", color = if (items.isEmpty()) p.faint else p.text, style = MaterialTheme.typography.titleLarge)
                                }
                                DropdownMenu(menu, { menu = false }) {
                                    items.forEach { (label, act) ->
                                        DropdownMenuItem(text = { Text(label) }, onClick = { menu = false; act() })
                                    }
                                }
                            }
                        }
                        if (investigators) actions.investigatorCard?.let { assign ->
                            InvestigatorCards(player, summary.bootId, enabled, compact = true, references = summary.investigatorReferences, useRelics = summary.useRelics, drawTwo = summary.drawTwoCards,
                                onDeal = actions.dealInvestigatorCards?.let { deal -> { slot -> deal(player, slot) } },
                                reservedCardIds = summary.players.flatMap { p -> p.dashboard.cardOffers.values.flatten() +
                                    InvestigatorCardSlot.entries.mapNotNull { p.dashboard.card(it)?.catalogueId } }.toSet(),
                                onAssign = { slot, card -> assign(player, slot, card) })
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
                if (actions.chooseStarter != null && actions.startWith == null && players.size >= 2) Text(
                    summary.players.firstOrNull { it.playerNumber == summary.starterPlayerNumber }?.let {
                        "${it.label}${if (settings.twoHeadedGiant) "'s team" else ""} goes first."
                    } ?: starterHint, color = p.muted, style = MaterialTheme.typography.bodySmall)
            }
            // The game folds to one tile once chosen; the mode stays one tap away.
            BrassCard {
                Eyebrow("Game")
                if (!settingsEnabled) Text("Add a player to choose the game.", color = p.muted)
                FoldingGamePicker(settings.profile, settingsEnabled, phoneHosted) { profile ->
                    actions.settings(profile, StandaloneGame.defaultLife(profile, settings.twoHeadedGiant && profile.magicRules), null)
                }
                if (settings.profile.magicRules) {
                    GameModeChips(settings.profile, GameMode.of(settings.profile, settings.twoHeadedGiant), settingsEnabled) { mode ->
                        if (mode != GameMode.of(settings.profile, settings.twoHeadedGiant)) {
                            val change = settingsForMode(settings.profile, mode)
                            actions.settings(change.profile, change.life, change.teams)
                        }
                    }
                }
                extraSettings()
                // Optional: the device goes round the table and each player makes their own choices.
                if (phoneHosted && players.isNotEmpty()) {
                    ToneButton("Guided setup: pass the device", { guided = true }, Modifier.fillMaxWidth(), enabled = enabled)
                    Text(if (settings.profile.deathMayDie) "Each player picks their investigator, sanity condition" +
                        (if (summary.useRelics) " and relic" else "") + " in turn, then everyone confirms."
                        else "Each player picks their own tile card and color in turn, then everyone confirms.",
                        color = p.faint, style = MaterialTheme.typography.bodySmall)
                }
            }
            // Rules fold away behind a title that already says the usual answer.
            val ruleSummary = listOfNotNull(
                "Rules",
                when {
                    settings.profile.deathMayDie -> null
                    settings.profile.pokemon -> "${settings.startingLife} Prize cards"
                    settings.profile.lorcana -> "${settings.startingLife} lore to win"
                    else -> "${settings.startingLife} life"
                },
                if (!settings.turnTimerEnabled) "No turn timer" else "${com.turnhub.android.domain.TurnTimerStatus.settingLabel(settings.turnTimerMs)} turns",
                "house rules".takeIf { official != null && settings.houseRules },
            ).joinToString(" · ")
            DisclosureCard(ruleSummary) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Text(when {
                        settings.profile.cooperative -> "Co-op: everyone wins or loses together"
                        officialMode == null -> "Custom mode"
                        locked || official == null -> "Official ${officialMode.name} rules"
                        else -> "House rules"
                    }, color = p.muted, style = MaterialTheme.typography.labelLarge, modifier = Modifier.weight(1f))
                    ModeRulesButton(settings.profile, settings.twoHeadedGiant)
                }
                var life by rememberSaveable(settings.startingLife) { mutableStateOf("${settings.startingLife}") }
                // Death May Die counts wounds up from 0: there is no starting number to set.
                if (!settings.profile.deathMayDie) Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    OutlinedTextField(life, { life = it.filter(Char::isDigit).take(5) },
                        label = { Text(when {
                            settings.profile.pokemon -> "Prize cards"
                            settings.profile.lorcana -> "Lore to win"
                            settings.twoHeadedGiant -> "Starting life per team"
                            else -> "Starting life"
                        }) },
                        singleLine = true, enabled = settingsEnabled && !locked, keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number),
                        modifier = Modifier.weight(1f))
                    ToneButton("Set", { life.toIntOrNull()?.let { actions.settings(null, it, null) } },
                        enabled = settingsEnabled && !locked && (life.toIntOrNull() ?: 0) > 0 && life != "${settings.startingLife}")
                }
                if (locked) Text(when {
                    settings.profile.pokemon -> "Official 6 Prize cards. Turn on house rules to change them."
                    settings.profile.lorcana -> "Official 20 lore to win. Turn on house rules to change it."
                    else -> "Official starting life. Turn on house rules to change it."
                }, color = p.faint,
                    style = MaterialTheme.typography.bodySmall)
                if (official != null) actions.houseRules?.let { setHouseRules ->
                    SetupSwitch("House rules", when {
                        settings.profile.pokemon -> "Change the Prize cards and player count. "
                        settings.profile.lorcana -> "Change the lore to win. "
                        else -> "Change the starting life, poison limit and player count. "
                    } +
                        "The game no longer follows the official ${official.name} rules.", settings.houseRules, settingsEnabled, setHouseRules)
                }
                TurnTimerChips(settings.turnTimerMs, settingsEnabled, actions.timer)
                if (settings.turnTimerEnabled) {
                    val rules = summary.localTimerRules
                    val changeRules = actions.timerRules
                    if (rules != null && changeRules != null) TimerRulesEditor(rules, settings.turnTimerMs, settingsEnabled, changeRules)
                    else Text("At the limit, Atlas keeps counting overtime.", color = p.muted, style = MaterialTheme.typography.bodySmall)
                }
                // Pokémon and Lorcana have no life total: prizes and lore end the game.
                if (settings.profile.countsLife) {
                    SetupSwitch("Automatic elimination at zero life", null, settings.autoEliminateAtZeroLife, settingsEnabled, actions.zeroLife)
                }
            }
            startHint?.let { Text(it, color = p.muted, style = MaterialTheme.typography.bodyMedium) }
            var askingFirst by rememberSaveable { mutableStateOf(false) }
            val startWith = actions.startWith?.takeIf { !settings.profile.cooperative }
            if (guided) GuidedSetupDialog(summary, actions, enabled && startReady, startHint,
                onStart = { guided = false; if (startWith != null) askingFirst = true else actions.start() },
                onClose = { guided = false })
            if (askingFirst && startWith != null) FirstPlayerPicker(players, settings.twoHeadedGiant,
                { player -> askingFirst = false; startWith(player) }) { askingFirst = false }
            StartTableButton(players.size, settings.twoHeadedGiant, enabled && startReady, settings.profile.starCommander,
                official?.players?.takeIf { locked }, settings.profile.bagOfDoom) {
                if (startWith != null) askingFirst = true else actions.start()
            }
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

/** A small accent link under a player's name for the step their seat still needs. */
@Composable
private fun SeatShortcut(label: String, description: String, enabled: Boolean, onClick: () -> Unit) {
    val p = palette
    val pill = androidx.compose.foundation.shape.RoundedCornerShape(99.dp)
    Text("+ $label", color = if (enabled) p.onAccent else p.faint, style = MaterialTheme.typography.labelLarge,
        fontWeight = androidx.compose.ui.text.font.FontWeight.Bold,
        modifier = Modifier
            .padding(top = 4.dp)
            .heightIn(min = 36.dp)
            .clip(pill)
            .background(if (enabled) p.accentBrush else androidx.compose.ui.graphics.SolidColor(p.fillStrong), pill)
            .clickable(enabled = enabled, role = androidx.compose.ui.semantics.Role.Button, onClick = onClick)
            .semantics { contentDescription = description }
            .padding(horizontal = 14.dp, vertical = 8.dp))
}

internal data class LobbyModeSettings(val profile: GameProfile?, val life: Int?, val teams: Boolean)

/** Both authorities receive the same format/life/team request for a mode change. */
internal fun settingsForMode(current: GameProfile, mode: GameMode): LobbyModeSettings {
    val profile = when {
        mode == GameMode.BAG_OF_DOOM -> GameProfile.MTG_BAG_OF_DOOM
        mode == GameMode.STAR_COMMANDER -> GameProfile.MTG_STAR_COMMANDER
        current.starCommander || current.bagOfDoom -> GameProfile.MTG_COMMANDER
        else -> null
    }
    val teams = mode == GameMode.TWO_HEADED_GIANT
    return LobbyModeSettings(profile, StandaloneGame.defaultLife(profile ?: current, teams), teams)
}
