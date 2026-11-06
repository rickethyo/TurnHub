package com.turnhub.android.ui.tablet

import androidx.compose.foundation.layout.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import com.turnhub.android.lan.*
import com.turnhub.android.standalone.*
import com.turnhub.android.ui.components.*
import com.turnhub.android.ui.theme.palette

/** The host and joined controllers render the same lobby; only their action adapter differs. */
@Composable
internal fun SharedRoomLobby(state: LanUiState, model: LanViewModel, onOpenPlanes: (() -> Unit)? = null) {
    val game = state.remote ?: return
    val summary = game.toSummary(System.currentTimeMillis(), com.turnhub.android.domain.TableClock.nowMs()).withLooks(state.looks)
    val enabled = state.online && !state.busy && !state.uncertain && (state.role in setOf("player", "table") || state.master)
    var selectedStarter by remember { mutableStateOf<Int?>(game.starter) }
    val startCountdown = rememberGameStartCountdown(game.state, enabled && game.canStart, revision = game.revision) {
        model.setup(game, "start", "index" to selectedStarter)
    }
    var creating by rememberSaveable { mutableStateOf(false) }
    if (creating) CreateProfileDialog(enabled, { name, credential ->
        if (!enabled) false else { model.setup(game, "create-profile", "name" to name, "credential" to credential?.let { org.json.JSONObject().put("salt", it.salt).put("hash", it.hash) }); true }
    }) { creating = false }
    TableLobby(summary, TableLobbyActions(
        add = { model.setup(game, "guest") },
        addProfile = { creating = true },
        forgetGuest = { model.setup(game, "forget-guest", "name" to it) },
        select = { if (it.guest) model.setup(game, "guest", "name" to it.name) else model.setup(game, "select", "profileId" to it.id) },
        remove = { model.setup(game, "remove", "index" to it.playerNumber - 1) },
        canRemove = { true }, clear = { model.setup(game, "clear") },
        swap = { a, b -> model.setup(game, "swap", "a" to a, "b" to b) },
        settings = { profile, life, teams ->
            model.setup(game, "configure", *listOfNotNull(profile?.let { "profile" to it.wireValue },
                life?.let { "life" to it }, teams?.let { "teams" to it }).toTypedArray())
        }, timer = { model.setup(game, "timer", "ms" to it) },
        zeroLife = { model.setup(game, "zero-life", "on" to it) },
        houseRules = { model.setup(game, "house-rules", "on" to it) },
        timerRules = { model.setup(game, "timer-rules", "timeout" to it.timeout.name, "earn" to it.earnExtensions,
            "percent" to it.earnBeforePercent, "max" to it.maxEarnedPerGame) },
        commander = { player, name, art, only -> model.setup(game, "commander", "index" to player.playerNumber - 1,
            "card" to LanCard(name, art, only).json()) },
        chooseStarter = { model.setup(game, "starter", "index" to it.playerNumber - 1) },
        start = { selectedStarter = game.starter; startCountdown() },
        startWith = { selectedStarter = it?.let { p -> p.playerNumber - 1 }; startCountdown() },
    ), savedPlayers = state.directory.filter { profile -> game.players.none { it.profileId == profile.id } }
        .map { LobbySavedPlayer(it.id, it.name, color = it.color, avatar = LocalPersonalization(avatar = it.avatar).icon()) } +
            state.guestNames.map { LobbySavedPlayer("guest:$it", it, guest = true) },
        enabled = enabled, settingsEnabled = enabled, canAdd = game.players.size < StandaloneGame.MAX_PLAYERS,
        identityHint = "Choose a profile or add a guest. Guests do not need a profile.", savedTitle = "Saved players",
        embedded = true, phoneHosted = true, startReady = game.canStart,
        savedSetups = { SharedSavedSetups(state, model) },
        feedback = { state.message?.let { Text(it, color = palette.warn) } },
        extraSettings = {
            if (game.profile.magicRules) {
                SetupSwitch("Planechase", "Plane cards and the planar die join the game.", game.planechase.enabled, enabled) {
                    model.setup(game, "planechase", "on" to it)
                }
                if (game.planechase.enabled && onOpenPlanes != null) ToneButton("Planar deck and rules", onOpenPlanes, Modifier.fillMaxWidth())
            }
        })
}
