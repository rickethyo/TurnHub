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
    val camera = rememberLauncherForActivityResult(ActivityResultContracts.RequestPermission()) { scanning = it }
    fun close() { if (!state.busy) { if (state.joined) confirmLeave = true else onClose() } }
    fun scan() {
        if (ContextCompat.checkSelfPermission(context, Manifest.permission.CAMERA) == PackageManager.PERMISSION_GRANTED) scanning = true
        else camera.launch(Manifest.permission.CAMERA)
    }
    if (state.remote != null) {
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
            val host = state.host
            when {
                host != null -> SharedHero(
                    eyebrow = "Play together · Hosting", title = sharedGameTitle(local),
                    subtitle = "This device owns the game",
                    mood = LinkMood.LIVE, reduceMotion = reduceMotion) {
                    StatusBadge(host.code?.let { "Join code $it" } ?: "Joining by QR only", Tone.GOOD)
                    StatusBadge(when (state.guests.size) { 0 -> "No devices yet"; 1 -> "1 device joined"; else -> "${state.guests.size} devices joined" })
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
                else -> person
            }
            val detail = when (guest.role) {
                "table" -> "Normal gameplay controls for every player"
                "display" -> "Read-only table view"
                "planes" -> "Face-up plane and planar die"
                else -> if (guest.player == 0) "Waiting for the next game" else "Personal player screen"
            }
            Column(Modifier.fillMaxWidth().clip(RoundedCornerShape(16.dp)).background(p.inset).padding(12.dp),
                verticalArrangement = Arrangement.spacedBy(8.dp)) {
                Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                    when (guest.role) {
                        "planes" -> PlanesMark(40.dp)
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
    if (state.profileOnly != null) {
        Text("This invitation connects another phone to the same player.", color = p.text)
        AccentButton("Connect to this player", { requestNetwork { model.join("player", 0) } }, enabled = !state.busy)
        return
    }
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
        // One row per profile ID: a profile on this device that the table already has is the same account.
        val mine = model.savedPlayers().filterNot { it.profileId in state.claimedProfiles }
        val atTable = state.directory.map { it.id }.toSet()
        val others = state.directory.filter { profile -> mine.none { it.profileId == profile.id } }
        if (mine.isNotEmpty()) Text("Your profiles on this device", color = p.muted)
        mine.forEach { player ->
            SeatRow({ PlayerAvatar(player.name, 1, null) }, player.name,
                if (player.profileId in atTable) "Already at this table · games sync" else "Bring your profile into this room", !state.busy) {
                local = player; selected = null; pin = ""; sourcePin = ""
            }
        }
        if (others.isNotEmpty()) Text("Profiles at this table", color = p.muted)
        others.forEach { profile ->
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
