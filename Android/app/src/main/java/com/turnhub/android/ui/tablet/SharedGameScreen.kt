package com.turnhub.android.ui.tablet

import android.Manifest
import android.content.pm.PackageManager
import android.graphics.Bitmap
import androidx.activity.compose.BackHandler
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.Image
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.platform.LocalClipboardManager
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.semantics.LiveRegionMode
import androidx.compose.ui.semantics.liveRegion
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.AnnotatedString
import androidx.compose.ui.unit.dp
import androidx.core.content.ContextCompat
import com.google.zxing.BarcodeFormat
import com.google.zxing.EncodeHintType
import com.google.zxing.MultiFormatWriter
import com.turnhub.android.domain.DeathMayDie
import com.turnhub.android.domain.InvestigatorDashboard
import com.turnhub.android.lan.LanUiState
import com.turnhub.android.lan.LanViewModel
import com.turnhub.android.protocol.TableState
import com.turnhub.android.standalone.StandaloneGame
import com.turnhub.android.ui.components.*
import com.turnhub.android.ui.theme.palette

/** A distinct screen prevents a guest's snapshot entering local play/history. */
@Composable
internal fun SharedGameScreen(state: LanUiState, local: StandaloneGame, model: LanViewModel,
    requestNetwork: (() -> Unit) -> Unit, reduceMotion: Boolean, onClose: () -> Unit) {
    val context = LocalContext.current
    var invitation by remember { mutableStateOf("") }
    var scanning by remember { mutableStateOf(false) }
    var confirmLeave by remember { mutableStateOf(false) }
    val camera = rememberLauncherForActivityResult(ActivityResultContracts.RequestPermission()) { scanning = it }
    fun close() { if (!state.busy) { if (state.remote != null) confirmLeave = true else onClose() } }
    BackHandler { close() }
    Column(Modifier.fillMaxSize().safeDrawingPadding().verticalScroll(rememberScrollState()).padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(14.dp)) {
        Eyebrow("Play together · same Wi-Fi")
        Text(if (state.host != null) "Hosting Death May Die" else if (state.remote != null) "Shared Death May Die" else "Host or join a game",
            style = MaterialTheme.typography.headlineLarge, color = palette.text)
        state.message?.let { Text(it, color = palette.warn, modifier = Modifier.semantics { liveRegion = LiveRegionMode.Polite }) }
        when {
            state.host != null -> {
                Text("Keep TurnHub open on this device. It owns the game, saves every change and records the result. Guests can join free.", color = palette.text)
                InviteQr(state.host.encode())
                val clipboard = LocalClipboardManager.current
                ToneButton("Copy invitation", { clipboard.setText(AnnotatedString(state.host.encode())) }, Modifier.fillMaxWidth())
                Text("Share only with your table. Each guest chooses the Elder One display or an investigator.", color = palette.muted)
                state.guests.forEach { guest -> BrassCard {
                    Text(if (guest.player == 0) "Elder One display" else local.players.getOrNull(guest.player - 1)?.let { "${it.name} · ${it.investigator}" } ?: "Investigator", color = palette.text)
                    ToneButton("Disconnect this device", { model.revoke(guest) }, Modifier.fillMaxWidth())
                } }
                ToneButton("Stop sharing", model::stopHosting, Modifier.fillMaxWidth())
            }
            state.remote != null -> {
                Text(if (state.online) "Connected to host" else "Disconnected · last known state", color = if (state.online) palette.good else palette.warn,
                    modifier = Modifier.semantics { liveRegion = LiveRegionMode.Polite })
                Text(if (state.remote.state == TableState.GAME_OVER) {
                    if (state.remote.winners.isEmpty()) "The investigators lost" else "The investigators won"
                } else "Round ${state.remote.round} · ${state.remote.state.name.lowercase()}", color = palette.muted)
                if (state.uncertain) ToneButton("Retry last action", { requestNetwork(model::retry) }, Modifier.fillMaxWidth(), enabled = !state.busy)
                if (state.role == "elder") {
                    Text("Elder One display · markers are controlled by the host", color = palette.muted)
                    ElderOneDisplay(state.remote, reduceMotion)
                } else InvestigatorRemote(state, model)
            }
            else -> {
                BrassCard {
                    Text("Host this device's game", color = palette.text, style = MaterialTheme.typography.titleLarge)
                    Text("Start Death May Die, then invite other devices on this Wi-Fi. Internet and Atlas aren't required.", color = palette.muted)
                    AccentButton("Host game", { requestNetwork(model::startHosting) }, Modifier.fillMaxWidth(),
                        enabled = !state.busy && local.profile.deathMayDie && local.state in listOf(TableState.RUNNING, TableState.PAUSED))
                }
                BrassCard {
                    Text("Join another device", color = palette.text, style = MaterialTheme.typography.titleLarge)
                    OutlinedTextField(invitation, { invitation = it }, label = { Text("Invitation from the host") },
                        modifier = Modifier.fillMaxWidth(), enabled = !state.busy)
                    ToneButton("Scan invitation", {
                        if (ContextCompat.checkSelfPermission(context, Manifest.permission.CAMERA) == PackageManager.PERMISSION_GRANTED) scanning = true
                        else camera.launch(Manifest.permission.CAMERA)
                    }, Modifier.fillMaxWidth(), enabled = !state.busy)
                    AccentButton(if (state.busy) "Connecting…" else "Find game", { requestNetwork { model.preview(invitation) } },
                        Modifier.fillMaxWidth(), enabled = !state.busy && invitation.isNotBlank())
                }
                state.preview?.let { game -> BrassCard {
                    Text("${game.elder.elderOne ?: "Death May Die"} · ${game.elder.episode ?: "Episode"}", color = palette.text,
                        style = MaterialTheme.typography.titleLarge)
                    ToneButton("Use as Elder One display", { requestNetwork { model.join("elder", 0) } }, Modifier.fillMaxWidth(), enabled = !state.busy)
                    game.players.forEachIndexed { index, player ->
                        ToneButton("Play ${player.investigator ?: player.name} · ${player.name}", { requestNetwork { model.join("investigator", index + 1) } },
                            Modifier.fillMaxWidth(), enabled = !state.busy)
                    }
                } }
            }
        }
        ToneButton(if (state.remote != null) "Leave shared game" else "Back to game", ::close, Modifier.fillMaxWidth(), enabled = !state.busy)
    }
    if (scanning) AlertDialog(onDismissRequest = { scanning = false }, title = { Text("Scan the host's invitation") },
        text = { QrScanner({ text ->
            if (runCatching { com.turnhub.android.lan.LanInvite.parse(text) }.isSuccess) {
                scanning = false; invitation = text; requestNetwork { model.preview(text) }
            }
        }, Modifier.fillMaxWidth().height(300.dp)) },
        confirmButton = { ToneButton("Cancel", { scanning = false }) })
    if (confirmLeave) AlertDialog(onDismissRequest = { confirmLeave = false }, title = { Text("Leave shared game?") },
        text = { Text(if (state.uncertain) "An action's reply is still unknown. Ask the host to check it before entering another change. Leaving releases this device's investigator session." else
            "The game and its history remain on the host. Your separate local game is unchanged.") },
        confirmButton = { ToneButton("Leave", { model.leave(); confirmLeave = false; onClose() }) },
        dismissButton = { ToneButton("Stay", { confirmLeave = false }) })
}

@Composable
private fun InviteQr(text: String) {
    val bitmap = remember(text) {
        val bits = MultiFormatWriter().encode(text, BarcodeFormat.QR_CODE, 512, 512, mapOf(EncodeHintType.MARGIN to 4))
        Bitmap.createBitmap(IntArray(512 * 512) { i -> if (bits[i % 512, i / 512]) android.graphics.Color.BLACK else android.graphics.Color.WHITE },
            512, 512, Bitmap.Config.ARGB_8888).asImageBitmap()
    }
    Box(Modifier.fillMaxWidth(), contentAlignment = Alignment.Center) {
        Image(bitmap, "Invitation granting access to this shared game. Scan using TurnHub on another device.", Modifier.sizeIn(maxWidth = 320.dp, maxHeight = 320.dp).aspectRatio(1f))
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun InvestigatorRemote(state: LanUiState, model: LanViewModel) {
    val game = state.remote ?: return
    val player = game.players.getOrNull(state.player - 1) ?: return
    val board = player.dashboard
    var confirmingDeath by remember(player.name, game.gameId) { mutableStateOf(false) }
    val enabled = state.online && !state.busy && !state.uncertain && !player.eliminated &&
        game.state in listOf(TableState.RUNNING, TableState.PAUSED)
    Text(player.investigator ?: player.name, style = MaterialTheme.typography.headlineLarge, color = palette.text)
    Text("${player.name} · " + when {
        player.eliminated -> "Out"
        game.active == state.player - 1 -> "Taking their turn"
        state.player - 1 in game.acted -> "Turn taken"
        else -> "Still to go"
    }, color = palette.muted)
    RemoteCounter("Wounds", player.life, DeathMayDie.DEATH_WOUNDS, enabled) { model.act("wounds", it) }
    RemoteCounter("Stress", player.stress, DeathMayDie.MAX_STRESS, enabled) { model.act("stress", it) }
    RemoteCounter("Fire", board.fire, InvestigatorDashboard.MAX_FIRE, enabled) { model.act("fire", it) }
    RemoteCounter("Insanity", board.insanity, InvestigatorDashboard.MAX_INSANITY, enabled) { model.act("insanity", it) }
    if (board.atThreshold) Text("Insanity threshold: resolve your sanity condition, then advance a skill.", color = palette.warn)
    if (board.insanity >= InvestigatorDashboard.MAX_INSANITY) Text("Consumed by madness. Confirm the investigator's death before continuing.", color = palette.bad)
    if (player.life >= DeathMayDie.DEATH_WOUNDS) Text("Six wounds: confirm the investigator's death before continuing.", color = palette.bad)
    if (player.life >= DeathMayDie.DEATH_WOUNDS || board.insanity >= InvestigatorDashboard.MAX_INSANITY)
        ToneButton("Confirm investigator death", { confirmingDeath = true }, Modifier.fillMaxWidth(), enabled = enabled)
    board.skills(player.investigator).forEachIndexed { index, skill -> BrassCard {
        val level = board.skillLevels.getOrElse(index) { 1 }
        Text("${skill.name} · Level $level", color = palette.text, style = MaterialTheme.typography.titleLarge)
        skill.levels.take(level).forEachIndexed { earlier, rules -> Text("${earlier + 1}. $rules", color = palette.text) }
        FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            (1..4).forEach { value -> ToneButton("Level $value", { model.act("skill", value, index + 1) }, enabled = enabled && value != level) }
        }
    } }
    listOfNotNull(board.sanityCondition, board.relic.takeIf { game.useRelics }).forEach { card -> BrassCard {
        Text(card.name, color = palette.text, style = MaterialTheme.typography.titleLarge)
        Text(card.rules.ifBlank { "Refer to the physical card." }, color = palette.text)
        if (card.transformedRules.isNotBlank()) {
            Eyebrow("Reverse face")
            Text(card.transformedRules, color = palette.text)
        }
    } }
    if (game.active != state.player - 1) ToneButton(if (game.stagedNextTurn == state.player - 1) "Cancel next turn" else "Take the next turn",
        { model.act("take-turn") }, Modifier.fillMaxWidth(), enabled = enabled && state.player - 1 !in game.acted && game.state == TableState.RUNNING)
    else {
        Text("Pass the turn to", color = palette.text, style = MaterialTheme.typography.titleMedium)
        game.toSummary(System.currentTimeMillis(), com.turnhub.android.domain.TableClock.nowMs()).passPlayerNumbers.forEach { target ->
            ToneButton(game.players[target - 1].investigator ?: game.players[target - 1].name, { model.act("pass", target) },
                Modifier.fillMaxWidth(), enabled = enabled && game.state == TableState.RUNNING)
        }
    }
    if (confirmingDeath) AlertDialog(onDismissRequest = { confirmingDeath = false }, title = { Text("Confirm investigator death?") },
        text = { Text("The host will remove this investigator. This can end the game under the Elder One rules.") },
        confirmButton = { ToneButton("Confirm death", { confirmingDeath = false; model.act("eliminate") }, enabled = enabled) },
        dismissButton = { ToneButton("Cancel", { confirmingDeath = false }) })
}

@Composable
private fun RemoteCounter(label: String, value: Int, maximum: Int, enabled: Boolean, onChange: (Int) -> Unit) {
    BrassCard {
        Text("$label · $value", color = palette.text, style = MaterialTheme.typography.headlineMedium,
            modifier = Modifier.semantics { liveRegion = LiveRegionMode.Polite })
        Row(horizontalArrangement = Arrangement.spacedBy(12.dp)) {
            ToneButton("− $label", { onChange(-1) }, Modifier.weight(1f), enabled = enabled && value > 0)
            ToneButton("+ $label", { onChange(1) }, Modifier.weight(1f), enabled = enabled && value < maximum)
        }
    }
}
