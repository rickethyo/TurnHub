package com.turnhub.android.ui.tablet

import android.Manifest
import android.content.pm.PackageManager
import android.graphics.Bitmap
import androidx.activity.compose.BackHandler
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.animation.animateColorAsState
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
import androidx.compose.foundation.text.TextAutoSize
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
import androidx.compose.ui.graphics.asImageBitmap
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
import androidx.compose.ui.unit.em
import androidx.core.content.ContextCompat
import com.google.zxing.BarcodeFormat
import com.google.zxing.EncodeHintType
import com.google.zxing.MultiFormatWriter
import com.turnhub.android.domain.DeathMayDie
import com.turnhub.android.domain.InvestigatorCard
import com.turnhub.android.domain.InvestigatorDashboard
import com.turnhub.android.lan.RoomProfile
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
    val elderView = state.view == "elder"
    val camera = rememberLauncherForActivityResult(ActivityResultContracts.RequestPermission()) { scanning = it }
    fun close() { if (!state.busy) { if (state.joined) confirmLeave = true else onClose() } }
    fun scan() {
        if (ContextCompat.checkSelfPermission(context, Manifest.permission.CAMERA) == PackageManager.PERMISSION_GRANTED) scanning = true
        else camera.launch(Manifest.permission.CAMERA)
    }
    if (state.remote?.profile?.let { !it.deathMayDie } == true || state.remote != null && !elderView && (state.role in setOf("display", "table", "planes") || state.view == "table")) {
        SharedTcgRemote(state, model, reduceMotion) { model.leave(); onClose() }
        return
    }
    BackHandler { close() }
    val p = palette
    AmbientBackground(reduceMotion, Modifier.fillMaxSize(), contentAlignment = Alignment.TopCenter) {
        Column(Modifier.widthIn(max = 720.dp).fillMaxSize().safeDrawingPadding().verticalScroll(rememberScrollState())
            .padding(16.dp), verticalArrangement = Arrangement.spacedBy(14.dp)) {
            if (state.reconnecting) {
                Text("Reconnecting to your room…", color = p.text)
                ToneButton("Leave room", { model.leave(); onClose() })
                return@Column
            }
            val remote = state.remote
            val host = state.host
            when {
                host != null -> SharedHero(
                    eyebrow = "Play together · Hosting", title = sharedGameTitle(local),
                    subtitle = local.elder.episode?.takeIf { local.profile.deathMayDie }?.let { "Episode · $it" } ?: "This device owns the game",
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
            if (state.resumable && state.host == null && !state.joined) {
                AccentButton("Resume room", { requestNetwork(model::resumeRoom) }, Modifier.fillMaxWidth(), enabled = !state.busy)
                ToneButton("End saved room", model::stopHosting, tone = Tone.BAD, enabled = !state.busy)
            }
            state.message?.let { Notice(it, p.warn, Modifier.semantics { liveRegion = LiveRegionMode.Polite }) }
            when {
                host != null -> HostingPanel(state, local, model)
                remote != null -> {
                    if (state.uncertain && !state.busy) Notice("The host hasn't confirmed your last action. Retry it to find out whether it was saved.", p.warn) {
                        ToneButton("Retry last action", { requestNetwork(model::retry) }, Modifier.fillMaxWidth(), tone = Tone.WARN, enabled = !state.busy)
                    }
                    ToneButton("Table view", { model.chooseView("table") })
                    if (state.role in setOf("player", "investigator")) Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                        ViewChoice("My investigator", !elderView, Modifier.weight(1f)) { model.chooseView("personal") }
                        ViewChoice("Elder One · read only", elderView, Modifier.weight(1f)) { model.chooseView("elder") }
                    }
                    if (state.role in setOf("elder", "display", "planes") || elderView) ElderOneDisplay(remote, reduceMotion) else InvestigatorRemote(state, model)
                }
                else -> {
                    val hostable = local.profile.lanSupported
                    val (qrOnly, setQrOnly) = rememberLanQrOnly()
                    BrassCard {
                        Eyebrow("Host from this device")
                        Text("Run the game on this device and invite friends to use their own phones. Everyone needs the same Wi-Fi; internet is optional.",
                            color = p.text, style = MaterialTheme.typography.bodyLarge)
                        if (!hostable) Text("Open a room now, then choose the game on the Game tab.",
                            color = p.muted, style = MaterialTheme.typography.bodyMedium)
                        LanSecuritySwitch(qrOnly, !state.busy, setQrOnly)
                        AccentButton("Host game", { requestNetwork { model.startHosting(qrOnly) } }, Modifier.fillMaxWidth(),
                            enabled = !state.busy)
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
                    if (state.preview != null) RoomPicker(state, model, requestNetwork)
                }
            }
            ToneButton(if (state.joined) "Leave shared game" else "Back to game", ::close, Modifier.fillMaxWidth(), enabled = !state.busy)
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
        text = { Text(if (state.uncertain) "An action's reply is still unknown. Ask the host to check it before entering another change. Leaving disconnects this device from the shared game." else
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
    val shape = RoundedCornerShape(24.dp)
    Box(Modifier.fillMaxWidth().heightIn(min = 172.dp).clip(shape)
        .background(Brush.verticalGradient(listOf(p.surface3, p.inset)))
        .border(1.dp, glow.copy(alpha = .55f), shape)) {
        SignalBeacon(glow, reduceMotion, Modifier.matchParentSize(), paused = mood == LinkMood.LOST)
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
            val localPlayer = local.players.getOrNull(guest.player - 1)
            val person = localPlayer?.name
                ?: guest.name?.takeIf { it.isNotBlank() } ?: "Player"
            val title = when (guest.role) {
                "table" -> "Shared table controls"
                "display" -> "Table display"
                "planes" -> "Planes display"
                "elder" -> "Elder One display"
                else -> person
            }
            val detail = when (guest.role) {
                "table" -> "Normal gameplay controls for every player"
                "display" -> "Read-only table view"
                "planes" -> "Face-up plane and planar die"
                "elder" -> "Track, ritual and stages"
                else -> if (guest.player == 0) "Waiting for the next game" else "Personal player screen"
            }
            Column(Modifier.fillMaxWidth().clip(RoundedCornerShape(16.dp)).background(p.inset).padding(12.dp),
                verticalArrangement = Arrangement.spacedBy(8.dp)) {
                Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                    when (guest.role) {
                        "planes" -> PlanesMark(40.dp)
                        "elder" -> ElderSigil(40.dp)
                        else -> PlayerAvatar(person, guest.player, null, size = 40.dp)
                    }
                    Column(Modifier.weight(1f)) {
                        Text(title, color = p.text, style = MaterialTheme.typography.titleMedium, fontWeight = FontWeight.Bold)
                        Text(if (guest.master) "Game master controls enabled" else detail, color = p.muted, style = MaterialTheme.typography.bodySmall)
                    }
                }
                Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    ToneButton(if (guest.master) "Remove game master" else "Make game master", { model.grantMaster(guest) }, Modifier.weight(1f))
                    ToneButton("Disconnect", { model.revoke(guest) }, Modifier.weight(1f), tone = Tone.BAD)
                }
            }
        }
    }
    ArmedButton("End room", "Tap again: disconnect every device", Tone.BAD, model::stopHosting)
}

/** The code in large spaced letters, readable across a table. */
@Composable
internal fun JoinCodeTile(code: String, modifier: Modifier = Modifier, compact: Boolean = false) {
    val p = palette
    val shape = RoundedCornerShape(if (compact) 14.dp else 20.dp)
    Box(modifier.fillMaxWidth().clip(shape).background(p.inset).border(2.dp, p.accent, shape)
        .padding(horizontal = 12.dp, vertical = if (compact) 8.dp else 18.dp).semantics(mergeDescendants = true) {
            contentDescription = "Join code " + code.toCharArray().joinToString(" ")
        }, contentAlignment = Alignment.Center) {
        val style = if (compact) MaterialTheme.typography.headlineSmall else MaterialTheme.typography.displayMedium
        Text(code.uppercase(), color = p.text, fontWeight = FontWeight.Black,
            modifier = Modifier.fillMaxWidth().clearAndSetSemantics { }, maxLines = 1, softWrap = false,
            autoSize = TextAutoSize.StepBased(minFontSize = 12.sp, maxFontSize = style.fontSize, stepSize = 1.sp),
            style = style.copy(letterSpacing = .08.em, textAlign = androidx.compose.ui.text.style.TextAlign.Center,
                fontFamily = androidx.compose.ui.text.font.FontFamily.Monospace))
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
internal fun InviteQr(text: String,
    description: String = "Invitation granting access to this shared game. Scan using TurnHub on another device.") {
    val bitmap = remember(text) {
        val bits = MultiFormatWriter().encode(text, BarcodeFormat.QR_CODE, 512, 512, mapOf(EncodeHintType.MARGIN to 2))
        Bitmap.createBitmap(IntArray(512 * 512) { i -> if (bits[i % 512, i / 512]) android.graphics.Color.BLACK else android.graphics.Color.WHITE },
            512, 512, Bitmap.Config.ARGB_8888).asImageBitmap()
    }
    // Always black on white in every theme: scanners need the contrast.
    Box(Modifier.fillMaxWidth(), contentAlignment = Alignment.Center) {
        Box(Modifier.sizeIn(maxWidth = 300.dp).fillMaxWidth().aspectRatio(1f).clip(RoundedCornerShape(20.dp))
            .background(Color.White).border(2.dp, palette.accent, RoundedCornerShape(20.dp)).padding(14.dp)) {
            Image(bitmap, description, Modifier.fillMaxSize())
        }
    }
}

/** Joining the room authorizes entry. Only protected profile linking asks for a PIN. */
@Composable
private fun RoomPicker(state: LanUiState, model: LanViewModel, requestNetwork: (() -> Unit) -> Unit) {
    val p = palette
    var selected by remember { mutableStateOf<RoomProfile?>(null) }
    var local by remember { mutableStateOf<com.turnhub.android.standalone.SavedLocalPlayer?>(null) }
    var pin by remember { mutableStateOf("") }
    var newName by remember { mutableStateOf("") }
    var sourcePin by remember { mutableStateOf("") }
    BrassCard {
        Eyebrow("Join the room")
        Text("Choose a screen or your profile. You stay connected between games.", color = p.text)
        SeatRow({ PlayerAvatar("Table", 0, null) }, "Table display", "Read only · no account needed", !state.busy) { requestNetwork { model.join("display", 0) } }
        SeatRow({ PlayerAvatar("Table", 0, null) }, "Shared table controls", "Facing-seat panels for everyone · no account needed", !state.busy) { requestNetwork { model.join("table", 0) } }
        SeatRow({ PlanesMark(44.dp) }, "Planes display", "Read only · available when this game uses Planechase", !state.busy) { requestNetwork { model.join("planes", 0) } }
        OutlinedTextField(value = newName, onValueChange = { newName = it.take(32) }, label = { Text("New player name") },
            singleLine = true, modifier = Modifier.fillMaxWidth())
        ToneButton("Join as a new player", { requestNetwork { model.joinNewProfile(newName) } }, enabled = !state.busy && newName.isNotBlank())
        if (state.preview?.profile?.deathMayDie == true) SeatRow({ ElderSigil(44.dp) }, "Elder One display",
            "Read only · track, ritual and stages", !state.busy) { requestNetwork { model.join("elder", 0) } }
        Text("Your profiles on this device", color = p.muted)
        model.savedPlayers().forEach { player ->
            SeatRow({ PlayerAvatar(player.name, 1, null) }, player.name, "Bring your profile into this room", !state.busy) {
                local = player; selected = null; pin = ""; sourcePin = ""
            }
        }
        Text("Profiles at this table", color = p.muted)
        state.directory.forEach { profile ->
            SeatRow({ PlayerAvatar(profile.name, 1, null) }, profile.name, "Use this profile" + if (profile.hasPin) " · PIN on first link" else "", !state.busy) {
                selected = profile; local = null; pin = ""
            }
        }
        if (selected != null || local != null) {
            Text("Join as ${selected?.name ?: local?.name}", color = p.text)
            if (local != null) {
                Text("If this is already you at the table, link to that profile for this room. Existing histories stay separate.", color = p.muted)
                state.directory.filter { it.id != local?.profileId }.forEach { profile ->
                    ToneButton("Link to ${profile.name}", { selected = profile; pin = "" }, enabled = !state.busy)
                }
                if (selected != null) ToneButton("Use my original profile", { selected = null; pin = "" })
            }
            if (local?.credential != null && selected != null) {
                OutlinedTextField(value = sourcePin, onValueChange = { sourcePin = it }, label = { Text("This device profile's PIN") },
                    visualTransformation = androidx.compose.ui.text.input.PasswordVisualTransformation(), singleLine = true, modifier = Modifier.fillMaxWidth())
            }
            if (selected?.hasPin == true || local?.credential != null && selected == null) {
                androidx.compose.material3.OutlinedTextField(value = pin, onValueChange = { pin = it }, label = { Text("Profile PIN or password") },
                    visualTransformation = androidx.compose.ui.text.input.PasswordVisualTransformation(), singleLine = true,
                    modifier = Modifier.fillMaxWidth())
            }
            AccentButton("Join with this profile", { requestNetwork {
                model.join("player", 0, profileId = selected?.id, pin = pin, localId = local?.localId, sourcePin = sourcePin.takeIf { selected != null && local != null })
            } }, Modifier.fillMaxWidth(), enabled = !state.busy)
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

/** The planes display's avatar: the Planes tab's cards icon in a disc. Decorative. */
@Composable
private fun PlanesMark(size: androidx.compose.ui.unit.Dp) {
    val p = palette
    Box(Modifier.size(size).clip(CircleShape).background(Brush.radialGradient(listOf(p.surface3, p.inset)))
        .border(2.dp, p.accent.copy(alpha = .7f), CircleShape).clearAndSetSemantics { }, contentAlignment = Alignment.Center) {
        androidx.compose.material3.Icon(androidx.compose.ui.res.painterResource(com.turnhub.android.ui.theme.DesignTokens.Icons.cards),
            contentDescription = null, tint = p.accent, modifier = Modifier.size(size * .5f))
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
    else {
        TurnCard(game, seat, player, enabled, model)
        GuestPauseButton(game.state, enabled, model, Modifier.fillMaxWidth())
    }
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
