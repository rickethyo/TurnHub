package com.turnhub.android.ui.tablet

import androidx.activity.compose.BackHandler
import androidx.compose.foundation.background
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.foundation.layout.*
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.*
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import com.turnhub.android.domain.TableClock
import com.turnhub.android.domain.TableSummary
import com.turnhub.android.lan.LanUiState
import com.turnhub.android.lan.LanViewModel
import com.turnhub.android.protocol.TableState
import com.turnhub.android.standalone.StandaloneGame
import com.turnhub.android.ui.components.*
import com.turnhub.android.ui.theme.palette

internal fun sharedGameTitle(game: StandaloneGame): String = if (game.profile.deathMayDie)
    listOfNotNull(game.elder.elderOne ?: "Death May Die", game.elder.episode).joinToString(" · ")
else when {
    game.profile.starCommander -> "Star Commander"
    game.profile.bagOfDoom -> "Bag of Doom"
    else -> FORMATS.firstOrNull { it.profile == game.profile }?.label ?: game.profile.wireValue
}

/** The same panels and player menu as the table, backed only by the host snapshot. */
@Composable
internal fun SharedTcgRemote(state: LanUiState, model: LanViewModel, reduceMotion: Boolean, onLeave: () -> Unit) {
    val game = state.remote ?: return
    val p = palette
    var tableView by rememberSaveable { mutableStateOf(false) }
    var menu by rememberSaveable { mutableStateOf(false) }
    var confirmLeave by remember { mutableStateOf(false) }
    var confirmElimination by remember { mutableStateOf(false) }
    val partners = remember { mutableStateListOf<Int>() }
    val haptics = rememberHaptics()
    val summary = game.toSummary(System.currentTimeMillis(), TableClock.nowMs()).withLooks(state.looks)
    val own = summary.players.firstOrNull { it.playerNumber == state.player }
    // Either teammate may pass. Keep the full host summary for the table and
    // show the claimed teammate as the personal panel's actor during their turn.
    val personal = if (own != null && summary.hasTurn(own.playerNumber)) summary.copy(activePlayerNumber = own.playerNumber) else summary
    val enabled = state.online && !state.busy && !state.uncertain && own != null && !own.eliminated &&
        game.state in listOf(TableState.RUNNING, TableState.PAUSED)
    // Navigation changes only this composable; token, polling and pending commands stay in the model.
    BackHandler {
        when { menu -> menu = false; tableView -> tableView = false; else -> confirmLeave = true }
    }
    fun act(kind: String, value: Int = 0, slot: Int = 0, source: Int = 0) {
        if (!tableView && enabled) model.act(kind, value, slot, source)
    }
    Column(Modifier.fillMaxSize().background(p.bg).safeDrawingPadding().padding(8.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            ToneButton(if (tableView) "My player" else "Table · read only", {
                menu = false; tableView = !tableView
            }, Modifier.weight(1f))
            ToneButton("Leave", { confirmLeave = true })
        }
        Text("${sharedGameTitle(game)} · ${if (tableView) "Table · read only" else own?.label ?: "My player"} · ${if (state.online) "Connected" else "Disconnected · last update"}",
            color = if (state.online) p.muted else p.warn, style = MaterialTheme.typography.labelLarge)
        state.message?.let { Text(it, color = p.warn) }
        when (game.state) {
            TableState.LOBBY -> Text("Waiting for the host to start. Your seat stays connected.", color = p.text)
            TableState.PAUSED -> Text("Paused by the table", color = p.warn)
            TableState.GAME_OVER -> Text(if (game.winners.isEmpty()) "Game over" else "Winner: " +
                game.winners.mapNotNull { game.players.getOrNull(it)?.name }.joinToString(), color = p.text)
            else -> Unit
        }
        if (state.uncertain) ToneButton("Retry last action", model::retry, enabled = !state.busy)
        if (state.busy) Text("Waiting for the host…", color = p.muted)
        if (!tableView && game.twoHeadedGiant) Text("Your team's life and poison are shared.", color = p.muted)
        // A claim from this device waits on the others; say so, and say when it was turned down.
        val claimant = summary.pending.winClaimPlayer
        val ownClaim = own != null && !own.eliminated && claimant == own.playerNumber
        var claimDenied by remember { mutableStateOf(false) }
        var wasClaiming by remember { mutableStateOf(false) }
        LaunchedEffect(ownClaim, game.state) {
            if (wasClaiming && !ownClaim && game.state != TableState.GAME_OVER && own?.eliminated == false) claimDenied = true
            if (ownClaim || game.state == TableState.GAME_OVER || game.state == TableState.LOBBY) claimDenied = false
            wasClaiming = ownClaim
        }
        if (ownClaim) Text(summary.players.firstOrNull { it.playerNumber == summary.pending.winConfirmationPlayer }
            ?.let { "Your win claim is waiting for ${it.label} to confirm." } ?: "Your win claim is waiting for confirmation.",
            color = p.warn)
        else if (claimDenied && game.state == TableState.RUNNING || claimDenied && game.state == TableState.PAUSED)
            Text("Your win claim was denied. Play continues.", color = p.warn)
        val visible = if (tableView) tableUnits(summary) else listOfNotNull(own?.let { TableUnit("own", listOf(it)) })
        BoxWithConstraints(Modifier.weight(1f).fillMaxWidth()) {
            val columns = if (tableView && maxWidth >= 600.dp && visible.size > 2) 2 else 1
            val rows = visible.chunked(columns)
            // A narrow screen can't fit every panel with its life total: scroll at a readable height.
            val scroll = rows.size > 1 && (maxHeight - 6.dp * (rows.size - 1)) / rows.size < MIN_REMOTE_PANEL
            Column(if (scroll) Modifier.fillMaxSize().verticalScroll(rememberScrollState()) else Modifier.fillMaxSize(),
                verticalArrangement = Arrangement.spacedBy(6.dp)) {
                rows.forEach { row ->
                    Row((if (scroll) Modifier.height(MIN_REMOTE_PANEL) else Modifier.weight(1f)).fillMaxWidth(),
                        horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                        row.forEach { unit ->
                            Box(Modifier.weight(1f).fillMaxHeight()) {
                                PlayerPanel(unit, if (tableView) summary else personal, unit.lifeSeat.life ?: 0, 0,
                                    onOpenMenu = { if (enabled && !tableView) menu = true }, onPlanarRoll = null,
                                    onBump = { act("life", it) }, onControl = { _, action -> act(action) },
                                    onTurnMark = { _, kind, value -> act(kind, value) },
                                    onSkillLevel = { _, _, _ -> }, onEditSkills = { _, _ -> }, onInvestigatorCard = { _, _, _ -> },
                                    investigatorFocused = false, onRespond = { _, _, _ -> }, offlineCommander = { _, _, _ -> 0 },
                                    haptics = haptics, reduceMotion = reduceMotion, controlsEnabled = enabled && !tableView)
                            }
                        }
                        if (row.size < columns) Spacer(Modifier.weight(1f))
                    }
                }
            }
            if (menu && !tableView && own != null) {
                PlayerMenu(TableUnit("own", listOf(own)), partners, personal,
                    onClose = { menu = false }, onControl = { _, action -> act(action) },
                    onEliminate = { _, _ -> menu = false; confirmElimination = true },
                    onCommander = { _, source, commander, delta -> act("commander", delta, commander, source) },
                    onCounter = { _, kind, value, slot -> act(kind, value,
                        if (kind.startsWith(StandaloneGame.ACTION_KIND) || kind.startsWith(StandaloneGame.CONDITION_KIND)) 0 else slot) },
                    onLife = { _, delta -> act("life", delta) }, onEditSkills = { _, _ -> },
                    onInvestigatorCard = { _, _, _ -> }, offlineCommander = { _, _, _ -> 0 }, controlsEnabled = enabled)
            }
        }
    }
    if (confirmLeave) AlertDialog(onDismissRequest = { confirmLeave = false }, title = { Text("Leave shared game?") },
        text = { Text(if (state.uncertain) "The last reply is unknown. Check the host before entering that change again."
            else "Your seat is released. The game stays on the host.") },
        confirmButton = { ToneButton("Leave", onLeave) }, dismissButton = { ToneButton("Stay", { confirmLeave = false }) })
    if (confirmElimination) AlertDialog(onDismissRequest = { confirmElimination = false },
        title = { Text(if (game.twoHeadedGiant) "Eliminate your team?" else "Eliminate your player?") },
        text = { Text("The host removes you from this game and may finish the match. It handles any elimination credit.") },
        confirmButton = { ToneButton("Eliminate", { confirmElimination = false; act("eliminate") }, enabled = enabled && !tableView) },
        dismissButton = { ToneButton("Cancel", { confirmElimination = false }) })
}

/** The host's colors, avatars and panel backgrounds on the guest's panels (by player order). */
internal fun TableSummary.withLooks(looks: List<com.turnhub.android.standalone.LocalPersonalization?>): TableSummary =
    if (looks.isEmpty()) this else copy(players = players.map { player ->
        val look = looks.getOrNull(player.playerNumber - 1) ?: return@map player
        player.copy(avatar = look.icon(), color = look.color, tileBackground = look.tileBackground)
    })

/** The shortest table panel that still shows its name band and the whole life total. */
private val MIN_REMOTE_PANEL = 200.dp
