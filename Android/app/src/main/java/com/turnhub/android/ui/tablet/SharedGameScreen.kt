package com.turnhub.android.ui.tablet

import android.Manifest
import android.content.pm.PackageManager
import android.graphics.Bitmap
import androidx.activity.compose.BackHandler
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.animation.animateColorAsState
import androidx.compose.animation.core.LinearEasing
import androidx.compose.animation.core.RepeatMode
import androidx.compose.animation.core.animateFloat
import androidx.compose.animation.core.infiniteRepeatable
import androidx.compose.animation.core.rememberInfiniteTransition
import androidx.compose.animation.core.snap
import androidx.compose.animation.core.tween
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.runtime.*
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.material3.TextButton
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.PathEffect
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.drawscope.rotate
import androidx.compose.ui.platform.LocalClipboardManager
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.semantics.LiveRegionMode
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.clearAndSetSemantics
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.liveRegion
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.AnnotatedString
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.core.content.ContextCompat
import com.google.zxing.BarcodeFormat
import com.google.zxing.EncodeHintType
import com.google.zxing.MultiFormatWriter
import com.turnhub.android.domain.DeathMayDie
import com.turnhub.android.domain.InvestigatorCard
import com.turnhub.android.domain.InvestigatorDashboard
import com.turnhub.android.lan.LanUiState
import com.turnhub.android.lan.LanViewModel
import com.turnhub.android.lan.lanSupported
import com.turnhub.android.protocol.TableState
import com.turnhub.android.standalone.LocalPlayer
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
    // An investigator device can look at the Elder One board and back; the session, its seat and
    // any pending retry stay as they are (owner, 2026-10-20).
    var elderView by rememberSaveable { mutableStateOf(false) }
    val camera = rememberLauncherForActivityResult(ActivityResultContracts.RequestPermission()) { scanning = it }
    fun close() { if (!state.busy) { if (state.remote != null) confirmLeave = true else onClose() } }
    fun scan() {
        if (ContextCompat.checkSelfPermission(context, Manifest.permission.CAMERA) == PackageManager.PERMISSION_GRANTED) scanning = true
        else camera.launch(Manifest.permission.CAMERA)
    }
    if (state.remote?.profile?.let { !it.deathMayDie } == true) {
        SharedTcgRemote(state, model, reduceMotion) { model.leave(); onClose() }
        return
    }
    BackHandler { close() }
    val p = palette
    Box(Modifier.fillMaxSize().tableBackground(p), contentAlignment = Alignment.TopCenter) {
        Column(Modifier.widthIn(max = 720.dp).fillMaxSize().safeDrawingPadding().verticalScroll(rememberScrollState())
            .padding(16.dp), verticalArrangement = Arrangement.spacedBy(14.dp)) {
            val remote = state.remote
            val host = state.host
            when {
                host != null -> SharedHero(
                    eyebrow = "Play together · Hosting", title = sharedGameTitle(local),
                    subtitle = local.elder.episode?.let { "Episode · $it" } ?: "This device owns the game",
                    mood = LinkMood.LIVE, reduceMotion = reduceMotion) {
                    StatusBadge(host.code?.let { "Join code $it" } ?: "Joining by QR only", Tone.GOOD)
                    StatusBadge(when (state.guests.size) { 0 -> "No devices yet"; 1 -> "1 device joined"; else -> "${state.guests.size} devices joined" })
                }
                remote != null -> {
                    val player = remote.players.getOrNull(state.player - 1)
                    SharedHero(
                        eyebrow = if (state.role == "elder") "Shared game · Elder One display" else "Shared game · Investigator",
                        title = if (state.role == "elder") remote.elder.elderOne ?: "Death May Die" else player?.let { it.investigator ?: it.name } ?: "Investigator",
                        subtitle = if (state.role == "elder") remote.elder.episode?.let { "Episode · $it" } ?: "Markers are controlled by the host"
                            else player?.let { "Played by ${it.name}" } ?: "",
                        mood = if (state.online) LinkMood.LIVE else LinkMood.LOST, reduceMotion = reduceMotion) {
                        StatusBadge(if (state.online) "Connected to host" else "Disconnected · last known state",
                            if (state.online) Tone.GOOD else Tone.WARN,
                            Modifier.semantics { liveRegion = LiveRegionMode.Polite })
                        StatusBadge(gameStatus(remote), if (remote.state == TableState.GAME_OVER) { if (remote.winners.isEmpty()) Tone.BAD else Tone.GOOD } else Tone.NEUTRAL)
                    }
                }
                else -> SharedHero(
                    eyebrow = "Play together · Same Wi-Fi", title = "Host or join a game",
                    subtitle = "One device owns the game. Join as your player or a shared display.",
                    mood = LinkMood.IDLE, reduceMotion = reduceMotion) {}
            }
            state.message?.let { Notice(it, p.warn, Modifier.semantics { liveRegion = LiveRegionMode.Polite }) }
            when {
                host != null -> HostingPanel(state, local, model)
                remote != null -> {
                    if (state.uncertain) Notice("The host hasn't confirmed your last action. Retry it to find out whether it was saved.", p.warn) {
                        ToneButton("Retry last action", { requestNetwork(model::retry) }, Modifier.fillMaxWidth(), tone = Tone.WARN, enabled = !state.busy)
                    }
                    if (state.role != "elder") Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                        ViewChoice("My investigator", !elderView, Modifier.weight(1f)) { elderView = false }
                        ViewChoice("Elder One · read only", elderView, Modifier.weight(1f)) { elderView = true }
                    }
                    if (state.role == "elder" || elderView) ElderOneDisplay(remote, reduceMotion) else InvestigatorRemote(state, model)
                }
                else -> {
                    val hostable = local.profile.lanSupported
                    val (qrOnly, setQrOnly) = rememberLanQrOnly()
                    BrassCard {
                        Eyebrow("Host from this device")
                        Text("Invite the table's other phones and tablets, from the lobby on. No internet or Atlas needed.",
                            color = p.text, style = MaterialTheme.typography.bodyLarge)
                        if (!hostable) Text("Choose a TCG or Death May Die game on this device first, then come back here to share it.",
                            color = p.muted, style = MaterialTheme.typography.bodyMedium)
                        LanSecuritySwitch(qrOnly, !state.busy, setQrOnly)
                        AccentButton("Host game", { requestNetwork { model.startHosting(qrOnly) } }, Modifier.fillMaxWidth(),
                            enabled = !state.busy && hostable)
                    }
                    BrassCard {
                        Eyebrow("Join another device")
                        Text("Enter the join code on the host's screen. If the host shows a QR code instead, scan it.",
                            color = p.text, style = MaterialTheme.typography.bodyLarge)
                        OutlinedTextField(invitation, { invitation = it.take(9) }, label = { Text("Join code, like abc-123") },
                            modifier = Modifier.fillMaxWidth(), enabled = !state.busy, singleLine = true,
                            textStyle = MaterialTheme.typography.headlineSmall.copy(letterSpacing = 3.sp, fontWeight = FontWeight.Bold),
                            keyboardOptions = androidx.compose.foundation.text.KeyboardOptions(
                                capitalization = androidx.compose.ui.text.input.KeyboardCapitalization.None,
                                autoCorrectEnabled = false, keyboardType = androidx.compose.ui.text.input.KeyboardType.Password))
                        AccentButton(if (state.busy) "Searching this Wi-Fi…" else "Find game", { requestNetwork { model.preview(invitation) } },
                            Modifier.fillMaxWidth(), enabled = !state.busy && com.turnhub.android.lan.LanJoinCode.normalize(invitation) != null)
                        ToneButton("Scan QR code", ::scan, Modifier.fillMaxWidth(), enabled = !state.busy)
                    }
                    state.preview?.let { game -> SeatPicker(game, enabled = !state.busy) { role, seat -> requestNetwork { model.join(role, seat) } } }
                }
            }
            ToneButton(if (remote != null) "Leave shared game" else "Back to game", ::close, Modifier.fillMaxWidth(), enabled = !state.busy)
        }
    }
    if (scanning) AlertDialog(onDismissRequest = { scanning = false }, title = { Text("Scan the host's invitation") },
        text = { QrScanner({ text ->
            if (runCatching { com.turnhub.android.lan.LanInvite.parse(text) }.isSuccess) {
                scanning = false; requestNetwork { model.preview(text) }
            }
        }, Modifier.fillMaxWidth().height(300.dp)) },
        confirmButton = { ToneButton("Cancel", { scanning = false }) })
    if (confirmLeave) AlertDialog(onDismissRequest = { confirmLeave = false }, title = { Text("Leave shared game?") },
        text = { Text(if (state.uncertain) "An action's reply is still unknown. Ask the host to check it before entering another change. Leaving releases this device's investigator session." else
            "The game and its history remain on the host. Your separate local game is unchanged.") },
        confirmButton = { ToneButton("Leave", { model.leave(); confirmLeave = false; onClose() }) },
        dismissButton = { ToneButton("Stay", { confirmLeave = false }) })
}

/** One of the investigator device's two views; the chosen one is filled and says so to TalkBack. */
@Composable
private fun ViewChoice(label: String, chosen: Boolean, modifier: Modifier, onClick: () -> Unit) {
    val m = modifier.semantics { selected = chosen }
    if (chosen) AccentButton(label, onClick, m) else ToneButton(label, onClick, m)
}

private fun gameStatus(game: StandaloneGame) = when (game.state) {
    TableState.GAME_OVER -> if (game.winners.isEmpty()) "The investigators lost" else "The investigators won"
    TableState.LOBBY -> "In the lobby"
    TableState.PAUSED -> "Round ${game.round} · Paused"
    TableState.RUNNING -> "Round ${game.round}"
    else -> "Round ${game.round} · ${game.state.name.lowercase().replace('_', ' ')}"
}

private enum class LinkMood { IDLE, LIVE, LOST }

/**
 * The screen's header: the game's name over rings spreading from a beacon, gold while
 * choosing, green while linked, amber once the host stops answering. The badges carry
 * the state; the rings and color only decorate it.
 */
@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun SharedHero(eyebrow: String, title: String, subtitle: String, mood: LinkMood, reduceMotion: Boolean,
    badges: @Composable FlowRowScope.() -> Unit) {
    val p = palette
    val target = when (mood) { LinkMood.IDLE -> p.accent; LinkMood.LIVE -> p.good; LinkMood.LOST -> p.warn }
    val glow by animateColorAsState(target, if (reduceMotion) snap() else tween(700), label = "linkGlow")
    val transition = rememberInfiniteTransition(label = "link")
    val wave by transition.animateFloat(0f, 1f, infiniteRepeatable(tween(3_200, easing = LinearEasing)), label = "wave")
    val turn by transition.animateFloat(0f, 360f, infiniteRepeatable(tween(48_000, easing = LinearEasing)), label = "turn")
    val breath by transition.animateFloat(.6f, 1f, infiniteRepeatable(tween(2_000), RepeatMode.Reverse), label = "breath")
    val still = reduceMotion || mood == LinkMood.LOST
    val shape = RoundedCornerShape(24.dp)
    Box(Modifier.fillMaxWidth().heightIn(min = 172.dp).clip(shape)
        .background(Brush.verticalGradient(listOf(p.surface3, p.inset)))
        .border(1.dp, glow.copy(alpha = .55f), shape)) {
        Canvas(Modifier.matchParentSize().clearAndSetSemantics { }) {
            val c = Offset(size.width * .86f, size.height * .38f)
            val r = size.height * .5f
            drawCircle(Brush.radialGradient(listOf(glow.copy(alpha = .3f), Color.Transparent), c, r * 1.5f), r * 1.5f, c)
            // Three rings spreading outward and fading, like a signal leaving the beacon.
            (0 until 3).forEach { i ->
                val t = if (still) (i + 1) / 3.5f else (wave + i / 3f) % 1f
                drawCircle(glow.copy(alpha = .55f * (1f - t)), r * (.25f + 1.15f * t), c, style = Stroke(1.5.dp.toPx()))
            }
            rotate(if (reduceMotion) 0f else turn, c) {
                drawCircle(glow.copy(alpha = .3f), r * .62f, c,
                    style = Stroke(1.dp.toPx(), pathEffect = PathEffect.dashPathEffect(floatArrayOf(6f, 10f))))
            }
            val pulse = if (still) .85f else breath
            drawCircle(Brush.radialGradient(listOf(glow.copy(alpha = pulse), glow.copy(alpha = 0f)), c, r * .22f), r * .22f, c)
            drawCircle(glow, r * .06f, c)
            drawRect(Brush.verticalGradient(0f to Color.White.copy(alpha = if (p.dark) .06f else .25f), .45f to Color.Transparent))
        }
        Column(Modifier.padding(20.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
            Text(eyebrow.uppercase(), color = glow, style = MaterialTheme.typography.labelMedium.copy(letterSpacing = 2.5.sp),
                fontWeight = FontWeight.Bold)
            Text(title, color = p.text, style = MaterialTheme.typography.headlineLarge, fontWeight = FontWeight.Black,
                modifier = Modifier.semantics { heading() })
            if (subtitle.isNotBlank()) Text(subtitle, color = p.muted, style = MaterialTheme.typography.titleSmall)
            FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalArrangement = Arrangement.spacedBy(6.dp),
                modifier = Modifier.padding(top = 4.dp), content = badges)
        }
    }
}

/** A bordered call-out, the same shape as the table's fire and death alerts. */
@Composable
private fun Notice(text: String, edge: Color, modifier: Modifier = Modifier, title: String? = null,
    content: @Composable ColumnScope.() -> Unit = {}) {
    val p = palette
    Column(modifier.fillMaxWidth().background(p.surface, RoundedCornerShape(14.dp))
        .border(2.dp, edge, RoundedCornerShape(14.dp)).padding(14.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
        title?.let { Text(it, color = edge, style = MaterialTheme.typography.titleMedium, fontWeight = FontWeight.Black,
            modifier = Modifier.semantics { heading() }) }
        Text(text, color = p.text, style = MaterialTheme.typography.bodyMedium)
        content()
    }
}

@Composable
private fun HostingPanel(state: LanUiState, local: StandaloneGame, model: LanViewModel) {
    val p = palette
    val host = state.host ?: return
    val clipboard = LocalClipboardManager.current
    BrassCard {
        if (host.code != null) {
            Eyebrow("Join code")
            Text("On each guest's device, open Game, then Play together, and enter this code. It stays the same until you stop sharing.",
                color = p.text, style = MaterialTheme.typography.bodyLarge)
            JoinCodeTile(host.code)
        } else if (host.qr != null) {
            Eyebrow("Scan to join")
            Text("This device only accepts guests that scan this code. On each guest's device, open Game, then Play together, then Scan QR code.",
                color = p.text, style = MaterialTheme.typography.bodyLarge)
            InviteQr(host.qr)
            ToneButton("Copy invitation", { clipboard.setText(AnnotatedString(host.qr)) }, Modifier.fillMaxWidth())
        }
        Text("Share it only with your table. Keep TurnHub open here: this device saves every change and records the result.",
            color = p.muted, style = MaterialTheme.typography.bodyMedium)
    }
    BrassCard {
        Eyebrow("Joined devices · ${state.guests.size}")
        if (state.guests.isEmpty()) Text("No one has joined yet. Devices appear here as they join.", color = p.muted,
            style = MaterialTheme.typography.bodyMedium)
        state.guests.forEach { guest ->
            val player = local.players.getOrNull(guest.player - 1)
            Row(Modifier.fillMaxWidth().clip(RoundedCornerShape(16.dp)).background(p.inset).padding(horizontal = 12.dp, vertical = 10.dp),
                verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                if (guest.player == 0) ElderSigil(40.dp) else PlayerAvatar(player?.name ?: "?", guest.player, null, size = 40.dp)
                Column(Modifier.weight(1f)) {
                    Text(if (guest.player == 0) "Elder One display" else player?.let { if (local.profile.deathMayDie) it.investigator ?: it.name else it.name } ?: "Player",
                        color = p.text, style = MaterialTheme.typography.titleMedium, fontWeight = FontWeight.Bold)
                    Text(if (guest.player == 0) "Shows the track and stages" else player?.name ?: "", color = p.muted,
                        style = MaterialTheme.typography.bodySmall)
                }
                ToneButton("Disconnect", { model.revoke(guest) }, tone = Tone.BAD)
            }
        }
    }
    ArmedButton("Stop sharing", "Tap again: disconnect every device", Tone.BAD, model::stopHosting)
}

/** The code in large spaced letters, readable across a table. */
@Composable
internal fun JoinCodeTile(code: String, modifier: Modifier = Modifier, compact: Boolean = false) {
    val p = palette
    val shape = RoundedCornerShape(if (compact) 14.dp else 20.dp)
    Box(modifier.fillMaxWidth().clip(shape).background(p.inset).border(2.dp, p.accent, shape)
        .padding(vertical = if (compact) 8.dp else 18.dp).semantics(mergeDescendants = true) {
            contentDescription = "Join code " + code.toCharArray().joinToString(" ")
        }, contentAlignment = Alignment.Center) {
        Text(code.uppercase(), color = p.text, fontWeight = FontWeight.Black,
            style = (if (compact) MaterialTheme.typography.headlineSmall else MaterialTheme.typography.displayMedium)
                .copy(letterSpacing = if (compact) 4.sp else 8.sp, fontFamily = androidx.compose.ui.text.font.FontFamily.Monospace))
    }
}

private const val QR_ONLY_PREF = "lan_qr_only"

/** This device's join security when it hosts: true lets only guests that scan its QR join. */
@Composable
internal fun rememberLanQrOnly(): Pair<Boolean, (Boolean) -> Unit> {
    val context = LocalContext.current
    val prefs = remember(context) { context.getSharedPreferences("turnhub_ui", android.content.Context.MODE_PRIVATE) }
    var on by remember { mutableStateOf(prefs.getBoolean(QR_ONLY_PREF, false)) }
    return on to { value: Boolean -> on = value; prefs.edit().putBoolean(QR_ONLY_PREF, value).apply() }
}

@Composable
internal fun LanSecuritySwitch(qrOnly: Boolean, enabled: Boolean, onChange: (Boolean) -> Unit) {
    SetupSwitch("Guests must scan a QR code",
        if (qrOnly) "Stronger: only devices that scan this screen can join. Applies the next time this device hosts."
        else "Off: guests type a short join code. Anyone on this Wi-Fi who learns the code can join.",
        qrOnly, enabled, onChange)
}

@Composable
private fun InviteQr(text: String) {
    val bitmap = remember(text) {
        val bits = MultiFormatWriter().encode(text, BarcodeFormat.QR_CODE, 512, 512, mapOf(EncodeHintType.MARGIN to 2))
        Bitmap.createBitmap(IntArray(512 * 512) { i -> if (bits[i % 512, i / 512]) android.graphics.Color.BLACK else android.graphics.Color.WHITE },
            512, 512, Bitmap.Config.ARGB_8888).asImageBitmap()
    }
    // Always black on white in every theme: scanners need the contrast.
    Box(Modifier.fillMaxWidth(), contentAlignment = Alignment.Center) {
        Box(Modifier.sizeIn(maxWidth = 300.dp).fillMaxWidth().aspectRatio(1f).clip(RoundedCornerShape(20.dp))
            .background(Color.White).border(2.dp, palette.accent, RoundedCornerShape(20.dp)).padding(14.dp)) {
            Image(bitmap, "Invitation granting access to this shared game. Scan using TurnHub on another device.", Modifier.fillMaxSize())
        }
    }
}

/** TCG player seats; Death May Die also offers the Elder One display. */
@Composable
private fun SeatPicker(game: StandaloneGame, enabled: Boolean, onJoin: (String, Int) -> Unit) {
    val p = palette
    BrassCard {
        Eyebrow("Found · choose your seat")
        Text(sharedGameTitle(game), color = p.text,
            style = MaterialTheme.typography.titleLarge, fontWeight = FontWeight.Bold)
        if (game.profile.deathMayDie) SeatRow({ ElderSigil(44.dp) }, "Elder One display", "A shared screen for the track and stages", enabled) { onJoin("elder", 0) }
        game.players.forEachIndexed { index, player ->
            SeatRow({ PlayerAvatar(player.name, index + 1, null) }, if (game.profile.deathMayDie) player.investigator ?: player.name else player.name,
                "Play as ${player.name}" + if (player.eliminated) " · out" else "", enabled) { onJoin(if (game.profile.deathMayDie) "investigator" else "player", index + 1) }
        }
    }
}

@Composable
private fun SeatRow(leading: @Composable () -> Unit, title: String, subtitle: String, enabled: Boolean, onClick: () -> Unit) {
    val p = palette
    val shape = RoundedCornerShape(16.dp)
    Row(Modifier.fillMaxWidth().heightIn(min = 64.dp).clip(shape).background(p.inset).border(1.dp, p.line, shape)
        .clickable(enabled = enabled, role = Role.Button, onClickLabel = "Join as $title", onClick = onClick)
        .padding(horizontal = 12.dp, vertical = 10.dp),
        verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(12.dp)) {
        leading()
        Column(Modifier.weight(1f)) {
            Text(title, color = if (enabled) p.text else p.faint, style = MaterialTheme.typography.titleMedium, fontWeight = FontWeight.Bold)
            Text(subtitle, color = p.muted, style = MaterialTheme.typography.bodySmall)
        }
        Text("Join ›", color = if (enabled) p.accent else p.faint, style = MaterialTheme.typography.labelLarge, fontWeight = FontWeight.Bold)
    }
}

/** A small seven-pointed summoning star standing in for the Elder One's avatar. Decorative. */
@Composable
private fun ElderSigil(size: androidx.compose.ui.unit.Dp) {
    val p = palette
    Box(Modifier.size(size).clip(CircleShape).background(Brush.radialGradient(listOf(p.surface3, p.inset)))
        .border(2.dp, p.bad.copy(alpha = .7f), CircleShape).clearAndSetSemantics { }) {
        Canvas(Modifier.matchParentSize()) {
            val c = center
            val r = this.size.minDimension * .34f
            val points = (0 until 7).map { i ->
                val a = Math.toRadians(i * 360.0 / 7 - 90).toFloat()
                Offset(c.x + r * kotlin.math.cos(a), c.y + r * kotlin.math.sin(a))
            }
            points.indices.forEach { i -> drawLine(p.bad, points[i], points[(i + 3) % 7], 1.5.dp.toPx()) }
            drawCircle(p.bad, r * .22f, c)
        }
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun InvestigatorRemote(state: LanUiState, model: LanViewModel) {
    val p = palette
    val game = state.remote ?: return
    val seat = state.player - 1
    val player = game.players.getOrNull(seat) ?: return
    val board = player.dashboard
    val enabled = state.online && !state.busy && !state.uncertain && !player.eliminated &&
        game.state in listOf(TableState.RUNNING, TableState.PAUSED)
    InvestigatorPortrait(player)
    if (game.state == TableState.LOBBY) Notice("You're in. Your controls unlock when the host starts the game.", p.info,
        title = "Waiting for the host to start")
    else TurnCard(game, seat, player, enabled, model)
    val dying = player.life >= DeathMayDie.DEATH_WOUNDS || board.insanity >= InvestigatorDashboard.MAX_INSANITY
    if (dying && !player.eliminated) Notice(
        if (player.life >= DeathMayDie.DEATH_WOUNDS) "Six wounds. Confirm the death, or remove a wound if it was a mistake. The host removes this investigator, which can end the game."
        else "Consumed by madness. Confirm the death, or correct insanity if it was a mistake. The host removes this investigator, which can end the game.",
        p.bad, Modifier.semantics { liveRegion = LiveRegionMode.Assertive },
        title = "☠ ${player.investigator ?: player.name} dies") {
        if (enabled) ArmedButton("Confirm investigator death", "Tap again: ${player.investigator ?: player.name} is out", Tone.BAD) { model.act("eliminate") }
    } else if (board.pendingLevelUps > 0) ThresholdAlert(player.investigator ?: player.name, board, board.skills(player.investigator), enabled) { slot, level ->
        model.act("skill", level, slot + 1)
    }
    BrassCard {
        Eyebrow("Tracks")
        // Two tiles a row where they fit with room for − and +; one a row on a phone's cover screen.
        BoxWithConstraints(Modifier.fillMaxWidth()) { FlowRow(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(10.dp),
            verticalArrangement = Arrangement.spacedBy(10.dp), maxItemsInEachRow = if (maxWidth < 480.dp) 1 else 2) {
            val tile = Modifier.weight(1f).fillMaxRowHeight()
            TrackTile("🩸 Wounds", "wound", player.life, DeathMayDie.DEATH_WOUNDS, p.bad, enabled, tile) { model.act("wounds", it) }
            TrackTile("⚡ Stress", "stress", player.stress, DeathMayDie.MAX_STRESS, p.info, enabled, tile) { model.act("stress", it) }
            // The End of Turn reminder sits under the Fire track it belongs to (owner, 2026-10-20).
            TrackTile("🔥 Fire", "fire token", board.fire, InvestigatorDashboard.MAX_FIRE, p.warn, enabled, tile, alarmAtMax = false,
                caption = if (board.fire > 0) "At End of Turn, roll ${board.fire} black ${if (board.fire == 1) "die" else "dice"}, then discard the fire."
                    else null) { model.act("fire", it) }
            TrackTile("🌀 Insanity", "insanity", board.insanity, InvestigatorDashboard.MAX_INSANITY, p.accent, enabled, tile,
                caption = "Bonus dice +${board.bonusDice}") { model.act("insanity", it) }
        } }
    }
    val skills = board.skills(player.investigator)
    var correcting by rememberSaveable(player.investigator) { mutableStateOf(false) }
    if (skills.isNotEmpty()) BrassCard {
        Eyebrow("Skills")
        skills.forEachIndexed { index, skill ->
            val level = board.skillLevels.getOrElse(index) { 1 }
            Column(Modifier.fillMaxWidth().clip(RoundedCornerShape(14.dp)).background(p.inset).padding(12.dp),
                verticalArrangement = Arrangement.spacedBy(6.dp)) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Text(skill.name, color = p.text, style = MaterialTheme.typography.titleMedium, fontWeight = FontWeight.Bold,
                        modifier = Modifier.weight(1f))
                    StatusBadge("Level $level", Tone.ACCENT)
                }
                // Levels rise from the threshold alert; the chips only fix a mistake (owner, 2026-10-20).
                if (correcting) SkillLevelChips(skill.name, level, enabled) { model.act("skill", it, index + 1) }
                skill.levels.take(level).forEachIndexed { earlier, rules ->
                    if (rules.isNotBlank()) Text("${earlier + 1}: $rules", color = p.text, style = MaterialTheme.typography.bodyMedium)
                }
            }
        }
        TextButton({ correcting = !correcting }) { Text(if (correcting) "Done correcting skill levels" else "Correct skill levels") }
    }
    board.sanityCondition?.let { CardPanel("🌀 Insanity card", it, p.accent) }
    board.relic.takeIf { game.useRelics }?.let { CardPanel("✦ Relic", it, p.warn) }
}

/** Whose turn it is, and this investigator's one turn action, first on the screen. */
@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun TurnCard(game: StandaloneGame, seat: Int, player: LocalPlayer, enabled: Boolean, model: LanViewModel) {
    val p = palette
    val active = game.active == seat
    val running = game.state == TableState.RUNNING
    BrassCard(highlight = if (active && running) p.active else null) {
        Eyebrow("Turn") {
            StatusBadge(when {
                player.eliminated -> "Out"
                active -> "Your turn"
                game.stagedNextTurn == seat -> "Up next"
                seat in game.acted -> "Turn taken"
                else -> "Still to go"
            }, when {
                player.eliminated -> Tone.BAD
                active -> Tone.ACTIVE
                seat in game.acted -> Tone.NEUTRAL
                else -> Tone.INFO
            })
        }
        if (!active) {
            game.active?.let(game.players::getOrNull)?.let { holder ->
                Text("${holder.investigator ?: holder.name} is taking their turn.", color = p.text, style = MaterialTheme.typography.bodyLarge)
            }
            val canTake = enabled && seat !in game.acted && running
            if (game.stagedNextTurn == seat) ToneButton("Cancel next turn", { model.act("take-turn") }, Modifier.fillMaxWidth(), enabled = canTake)
            else AccentButton("Take the next turn", { model.act("take-turn") }, Modifier.fillMaxWidth(), enabled = canTake)
        } else {
            Text("When you're done, pass the turn to", color = p.text, style = MaterialTheme.typography.titleMedium)
            FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                game.toSummary(System.currentTimeMillis(), com.turnhub.android.domain.TableClock.nowMs()).passPlayerNumbers.forEach { target ->
                    val next = game.players[target - 1]
                    ToneButton(next.investigator ?: next.name, { model.act("pass", target) }, enabled = enabled && running)
                }
            }
        }
    }
}

/** One track: its name, the number out of [max], a bar of the same, and − / + either side. */
@Composable
private fun TrackTile(name: String, noun: String, value: Int, max: Int, hue: Color, enabled: Boolean, modifier: Modifier,
    alarmAtMax: Boolean = true, caption: String? = null, onStep: (Int) -> Unit) {
    val p = palette
    val full = alarmAtMax && value >= max
    val shape = RoundedCornerShape(18.dp)
    Column(modifier.clip(shape).background(Brush.verticalGradient(listOf(hue.copy(alpha = .16f), p.inset)))
        .border(if (full) 2.dp else 1.dp, if (full) p.bad else hue.copy(alpha = .55f), shape).padding(12.dp),
        verticalArrangement = Arrangement.spacedBy(8.dp)) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            StepButton("−", "Remove a $noun", enabled && value > 0) { onStep(-1) }
            Column(Modifier.weight(1f).semantics(mergeDescendants = true) { liveRegion = LiveRegionMode.Polite },
                horizontalAlignment = Alignment.CenterHorizontally) {
                Text(name.uppercase(), color = p.text, style = MaterialTheme.typography.labelSmall.copy(letterSpacing = 1.5.sp), maxLines = 1)
                Row(verticalAlignment = Alignment.Bottom) {
                    Text("$value", color = if (full) p.bad else p.text, style = MaterialTheme.typography.headlineMedium, fontWeight = FontWeight.Black)
                    Text(" / $max", color = p.muted, style = MaterialTheme.typography.labelLarge, modifier = Modifier.padding(bottom = 4.dp))
                }
            }
            StepButton("+", "Add a $noun", enabled && value < max) { onStep(1) }
        }
        TrackBar(value, max, hue)
        caption?.let { Text(it, color = p.text, style = MaterialTheme.typography.labelMedium, textAlign = androidx.compose.ui.text.style.TextAlign.Center,
            modifier = Modifier.align(Alignment.CenterHorizontally)) }
    }
}

/** Pips for short tracks, a filling bar for long ones. Decorative: the number says it. */
@Composable
private fun TrackBar(value: Int, max: Int, hue: Color) {
    val p = palette
    if (max <= 9) Row(Modifier.fillMaxWidth().clearAndSetSemantics { }, horizontalArrangement = Arrangement.spacedBy(4.dp)) {
        (1..max).forEach { i ->
            Box(Modifier.weight(1f).height(6.dp).clip(RoundedCornerShape(3.dp)).background(if (i <= value) hue else p.line))
        }
    } else Box(Modifier.fillMaxWidth().height(6.dp).clip(RoundedCornerShape(3.dp)).background(p.line).clearAndSetSemantics { }) {
        Box(Modifier.fillMaxWidth(value.toFloat() / max).fillMaxHeight().background(hue))
    }
}

@Composable
private fun CardPanel(slot: String, card: InvestigatorCard, hue: Color) {
    val p = palette
    val shape = RoundedCornerShape(22.dp)
    // The insanity card and the relic each wear their own color and mark, so they're told apart at a glance.
    Column(Modifier.fillMaxWidth().clip(shape).background(Brush.verticalGradient(listOf(hue.copy(alpha = .22f), p.surface)))
        .border(2.dp, hue, shape).padding(16.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
        Text(slot.uppercase(), color = hue, style = MaterialTheme.typography.labelLarge.copy(letterSpacing = 1.5.sp), fontWeight = FontWeight.Black,
            modifier = Modifier.semantics { heading() })
        Text(card.name, color = p.text, style = MaterialTheme.typography.titleLarge, fontWeight = FontWeight.Bold)
        Text(card.rules.ifBlank { "Refer to the physical card." }, color = p.text, style = MaterialTheme.typography.bodyMedium)
        if (card.transformedRules.isNotBlank()) {
            Column(Modifier.fillMaxWidth().clip(RoundedCornerShape(14.dp)).background(p.inset).padding(12.dp),
                verticalArrangement = Arrangement.spacedBy(4.dp)) {
                Text("REVERSE FACE", color = p.muted, style = MaterialTheme.typography.labelSmall.copy(letterSpacing = 1.5.sp))
                Text(card.transformedRules, color = p.text, style = MaterialTheme.typography.bodyMedium)
            }
        }
        InvestigatorCardReferenceButton(card.catalogueId)
    }
}

/** The investigator's picture and name at the top of their device, when the wiki has one. */
@Composable
private fun InvestigatorPortrait(player: LocalPlayer) {
    val p = palette
    val art = rememberInvestigatorArt(player.investigator) ?: return
    val shape = RoundedCornerShape(22.dp)
    Box(Modifier.fillMaxWidth().height(220.dp).clip(shape).border(1.dp, p.line, shape)) {
        Image(art.image, contentDescription = "${art.cardName}, art by ${art.artist}", Modifier.fillMaxSize(),
            contentScale = androidx.compose.ui.layout.ContentScale.Crop, alignment = Alignment.TopCenter)
        Box(Modifier.fillMaxSize().background(Brush.verticalGradient(listOf(Color.Transparent, Color.Black.copy(alpha = .7f)))))
        Text(player.investigator ?: player.name, color = Color.White, style = MaterialTheme.typography.headlineSmall, fontWeight = FontWeight.Black,
            modifier = Modifier.align(Alignment.BottomStart).padding(16.dp))
    }
}

/** The host's join code above the Game and Table tabs while it shares, so the table can read it any time. */
@Composable
internal fun SharedCodeBanner(host: com.turnhub.android.lan.LanHosting, guests: Int, onOpen: () -> Unit) {
    val p = palette
    val shape = RoundedCornerShape(16.dp)
    Row(Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 8.dp).clip(shape).background(p.surface)
        .border(1.dp, p.good.copy(alpha = .6f), shape)
        .clickable(role = Role.Button, onClickLabel = "Open Play together", onClick = onOpen)
        .padding(horizontal = 14.dp, vertical = 10.dp),
        verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(12.dp)) {
        Column(Modifier.weight(1f)) {
            Text("PLAY TOGETHER", color = p.good, style = MaterialTheme.typography.labelSmall.copy(letterSpacing = 1.5.sp), fontWeight = FontWeight.Bold)
            Text(when (guests) { 0 -> "No devices joined yet"; 1 -> "1 device joined"; else -> "$guests devices joined" },
                color = p.muted, style = MaterialTheme.typography.bodySmall)
        }
        if (host.code != null) JoinCodeTile(host.code, Modifier.width(170.dp), compact = true)
        else Text("Scan the QR in Play together", color = p.text, style = MaterialTheme.typography.labelLarge)
    }
}
