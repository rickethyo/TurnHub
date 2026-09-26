package com.turnhub.android.ui.home

import android.Manifest
import android.content.pm.PackageManager
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.height
import androidx.compose.ui.platform.LocalContext
import androidx.core.content.ContextCompat
import com.turnhub.android.data.presenceCodeFromQr
import com.turnhub.android.ui.components.QrScanner

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.selection.toggleable
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Checkbox
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.semantics.LiveRegionMode
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.liveRegion
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.text.input.VisualTransformation
import androidx.compose.ui.unit.dp
import com.turnhub.android.data.AccountInfo
import com.turnhub.android.data.AdminState
import com.turnhub.android.data.AtlasAdminConsole
import com.turnhub.android.data.DeviceInfo
import com.turnhub.android.protocol.AccountPermission
import com.turnhub.android.protocol.AvatarIcon
import com.turnhub.android.protocol.SessionInfo
import com.turnhub.android.ui.components.AccentButton
import com.turnhub.android.ui.components.BrassCard
import com.turnhub.android.ui.components.Eyebrow
import com.turnhub.android.ui.components.PlayerAvatar
import com.turnhub.android.ui.components.StatusBadge
import com.turnhub.android.ui.components.StatusRow
import com.turnhub.android.ui.components.Tone
import com.turnhub.android.ui.components.ToneButton
import com.turnhub.android.ui.theme.palette
import java.security.SecureRandom

/** Admin and developer callbacks. [run] sends one request through the admin console. */
data class AdminActions(
    val onRefresh: () -> Unit = {},
    val onDeveloperRefresh: () -> Unit = {},
    val run: (suspend AtlasAdminConsole.() -> Unit) -> Unit = {},
    val onDismissMessage: () -> Unit = {},
    val onDownloadLog: () -> Unit = {},
)

/** Speaker levels as the portal names them (`/api/speaker`, 0-3). */
private val SPEAKER_LEVELS = listOf(0 to "Off", 1 to "Low", 2 to "Medium (default)", 3 to "High")

/** The portal's Device Settings tab plus its Game Master panel, for Admin and Game Master accounts. */
@Composable
fun SettingsTab(info: SessionInfo, admin: AdminState, avatars: List<AvatarIcon>, actions: AdminActions) {
    LaunchedEffect(info.profileId, info.permissions) { actions.onRefresh() }
    val isAdmin = info.has(AccountPermission.ADMIN)
    Column(verticalArrangement = Arrangement.spacedBy(14.dp)) {
        AdminMessage(admin, actions)
        if (isAdmin) {
            PresenceCard(admin, actions)
            WifiCard(admin, actions)
            SigilsCard(admin, actions)
            TableAndAtlasCard(admin, actions)
            AccountsCard(admin, avatars, actions)
        }
        if (info.has(AccountPermission.GAME_MASTER)) GameMasterCard(info, admin, avatars, actions)
    }
}

@Composable
internal fun AdminMessage(admin: AdminState, actions: AdminActions) {
    val p = palette
    val message = admin.message ?: return
    Row(
        Modifier
            .fillMaxWidth()
            .clip(RoundedCornerShape(12.dp))
            .background(p.surface2)
            .border(1.dp, if (message.isError) p.bad else p.good, RoundedCornerShape(12.dp))
            .padding(12.dp)
            .semantics(mergeDescendants = true) { liveRegion = LiveRegionMode.Polite },
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(message.message, color = if (message.isError) p.bad else p.good, modifier = Modifier.weight(1f))
        TextButton(onClick = actions.onDismissMessage) { Text("OK", color = p.text) }
    }
}

/** Asks for the six-digit code the Atlas screen shows (table presence). */
@Composable
fun PresenceCodeDialog(admin: AdminState, actions: AdminActions) {
    if (!admin.codePrompt) return
    val p = palette
    val context = LocalContext.current
    var code by remember { mutableStateOf("") }
    var scanning by remember { mutableStateOf(false) }
    var scanHint by remember { mutableStateOf<String?>(null) }
    val cameraPermission = rememberLauncherForActivityResult(ActivityResultContracts.RequestPermission()) { granted ->
        scanning = granted
        if (!granted) scanHint = "Camera permission is off. Type the code instead."
    }
    val startScan = {
        scanHint = null
        if (ContextCompat.checkSelfPermission(context, Manifest.permission.CAMERA) == PackageManager.PERMISSION_GRANTED) {
            scanning = true
        } else {
            cameraPermission.launch(Manifest.permission.CAMERA)
        }
    }
    AlertDialog(
        onDismissRequest = { actions.run { dismissCode() } },
        containerColor = p.surface,
        title = { Text("Verify at the table") },
        text = {
            Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
                Text(
                    "The Atlas screen now shows a six-digit code and a QR code for you. Scan the QR or type the code " +
                        "to prove you are at the table. You stay verified for 10 minutes.",
                    color = p.muted,
                )
                if (scanning) {
                    QrScanner(
                        onScanned = { text ->
                            val scanned = presenceCodeFromQr(text)
                            if (scanned != null) {
                                scanning = false
                                code = scanned
                                actions.run { confirmCode(scanned) }
                            } else {
                                scanHint = "That QR isn't the Admin code. Scan the one next to the six digits."
                            }
                        },
                        modifier = Modifier.fillMaxWidth().height(240.dp).clip(RoundedCornerShape(12.dp)),
                    )
                    ToneButton("Stop scanning", { scanning = false }, Modifier.fillMaxWidth())
                } else {
                    ToneButton("Scan the QR code", startScan, Modifier.fillMaxWidth(), enabled = !admin.busy)
                }
                scanHint?.let { Text(it, color = p.muted, style = MaterialTheme.typography.bodySmall) }
                OutlinedTextField(
                    value = code,
                    onValueChange = { code = it.filter(Char::isDigit).take(6) },
                    label = { Text("Code from the Atlas screen") },
                    singleLine = true,
                    keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.NumberPassword),
                    textStyle = MaterialTheme.typography.headlineSmall.copy(fontFamily = FontFamily.Monospace),
                )
                admin.message?.takeIf { it.isError }?.let { Text(it.message, color = p.bad) }
            }
        },
        confirmButton = {
            TextButton(onClick = { val c = code; actions.run { confirmCode(c) } }, enabled = code.length == 6 && !admin.busy) {
                Text("Verify", fontWeight = FontWeight.Bold)
            }
        },
        dismissButton = { TextButton(onClick = { actions.run { dismissCode() } }) { Text("Cancel", color = p.text) } },
    )
}

@Composable
private fun PresenceCard(admin: AdminState, actions: AdminActions) {
    val p = palette
    val presence = admin.presence
    BrassCard {
        Eyebrow("Verified at the table")
        Text(
            "Renaming Sigils, the Wi-Fi password, Return to lobby and factory resets need proof you are at the table: " +
                "Atlas shows a code on its screen and you enter it here.",
            color = p.muted,
            style = MaterialTheme.typography.bodySmall,
        )
        if (presence?.verified == true) {
            StatusBadge("Verified · ${(presence.remainingMs / 60_000) + 1} min left", Tone.GOOD)
        } else {
            StatusBadge("Not verified", Tone.WARN)
        }
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            AccentButton("Verify at the table", { actions.run { requestCode() } }, Modifier.weight(1f), enabled = !admin.busy)
            ToneButton("Stop", { actions.run { lockPresence() } }, Modifier.weight(1f), enabled = presence?.verified == true)
        }
    }
}

@Composable
private fun WifiCard(admin: AdminState, actions: AdminActions) {
    val p = palette
    val net = admin.network
    var password by remember { mutableStateOf("") }
    var show by remember { mutableStateOf(false) }
    var confirm by remember { mutableStateOf(false) }
    if (confirm) {
        ConfirmDialog(
            title = "Change the Wi-Fi password?",
            text = "Atlas restarts and every connected device is disconnected until it rejoins with the new password. " +
                "This app will need the new password too.",
            confirm = "Save & restart",
            onConfirm = { confirm = false; val pw = password; actions.run { saveNetworkPassword(pw) } },
            onDismiss = { confirm = false },
        )
    }
    BrassCard {
        Eyebrow("Atlas Wi-Fi security")
        StatusRow("Network", net?.ssid ?: "—")
        StatusRow("Security", net?.security ?: "—")
        StatusRow("Connected clients", net?.stations?.toString() ?: "—")
        StatusRow(
            "Password",
            when {
                net == null -> "—"
                net.passwordIsDefault -> "Factory default (change it)"
                else -> "${net.passwordLength} characters"
            },
        )
        OutlinedTextField(
            value = password,
            onValueChange = { password = it.take(63) },
            label = { Text("New Wi-Fi password (8–63 characters)") },
            singleLine = true,
            visualTransformation = if (show) VisualTransformation.None else PasswordVisualTransformation(),
            modifier = Modifier.fillMaxWidth(),
        )
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            ToneButton("Generate", { password = generatePassword(); show = true }, Modifier.weight(1f))
            ToneButton(if (show) "Hide" else "Show", { show = !show }, Modifier.weight(1f))
        }
        ToneButton("Save & restart Atlas", { confirm = true }, Modifier.fillMaxWidth(), tone = Tone.WARN,
            enabled = password.length in 8..63 && !admin.busy)
    }
}

private fun generatePassword(): String {
    // No look-alike characters, so it can be read off a screen.
    val alphabet = "ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnpqrstuvwxyz23456789"
    val random = SecureRandom()
    return (1..16).map { alphabet[random.nextInt(alphabet.length)] }.joinToString("")
        .chunked(4).joinToString("-")
}

@Composable
private fun SigilsCard(admin: AdminState, actions: AdminActions) {
    val p = palette
    var rename by remember { mutableStateOf<DeviceInfo?>(null) }
    var forget by remember { mutableStateOf<DeviceInfo?>(null) }
    var reset by remember { mutableStateOf<DeviceInfo?>(null) }
    var forgetAll by remember { mutableStateOf(false) }
    rename?.let { device ->
        var name by remember(device.id) { mutableStateOf(device.customName) }
        AlertDialog(
            onDismissRequest = { rename = null },
            containerColor = p.surface,
            title = { Text("Rename ${device.label}") },
            text = {
                OutlinedTextField(
                    value = name,
                    onValueChange = { name = it.take(32) },
                    label = { Text("Custom name (empty for ${device.defaultLabel})") },
                    singleLine = true,
                )
            },
            confirmButton = {
                TextButton(onClick = {
                    rename = null
                    val n = name.trim()
                    actions.run { renameDevice(device.id, n) }
                }) { Text("Save name", fontWeight = FontWeight.Bold) }
            },
            dismissButton = { TextButton(onClick = { rename = null }) { Text("Cancel", color = p.text) } },
        )
    }
    forget?.let { device ->
        ConfirmDialog(
            "Forget this Sigil?",
            "Atlas forgets “${device.label}”. Pair it again (Pair a Sigil on the Atlas screen, then the Sigil's Pair button) before anyone uses it.",
            "Forget Sigil",
            onConfirm = { forget = null; actions.run { forgetDevice(device.id) } },
            onDismiss = { forget = null },
        )
    }
    reset?.let { device ->
        ConfirmDialog(
            "Factory reset this Sigil?",
            "“${device.label}” erases everything it has saved, including its pairing, and restarts. Atlas forgets it.",
            "Factory reset",
            onConfirm = { reset = null; actions.run { factoryResetSigil(device.id) } },
            onDismiss = { reset = null },
        )
    }
    if (forgetAll) {
        ConfirmDialog(
            "Forget all Sigils?",
            "Atlas forgets every paired Sigil. Each one must be paired again before anyone can use it.",
            "Forget all",
            onConfirm = { forgetAll = false; actions.run { forgetDevice(null) } },
            onDismiss = { forgetAll = false },
        )
    }
    BrassCard {
        Eyebrow("Paired Sigils")
        Text(
            "Forget works in the lobby, for Sigils nobody is seated on. Holding a Sigil's own Pair button for 10 seconds " +
                "makes the Sigil forget Atlas.",
            color = p.muted,
            style = MaterialTheme.typography.bodySmall,
        )
        admin.atlasHardwareId?.let { StatusRow("Atlas", it) }
        if (admin.devices.isEmpty()) EmptyNote("No Sigils discovered yet.")
        admin.devices.forEach { device -> DeviceRow(device, { rename = device }, { forget = device }, { reset = device }) }
        ToneButton("Forget all Sigils", { forgetAll = true }, Modifier.fillMaxWidth(), tone = Tone.WARN,
            enabled = admin.devices.isNotEmpty())
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun DeviceRow(device: DeviceInfo, onRename: () -> Unit, onForget: () -> Unit, onReset: () -> Unit) {
    val p = palette
    Column(
        Modifier
            .fillMaxWidth()
            .clip(RoundedCornerShape(14.dp))
            .background(p.surface2)
            .border(1.dp, p.line, RoundedCornerShape(14.dp))
            .padding(12.dp),
        verticalArrangement = Arrangement.spacedBy(8.dp),
    ) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Column(Modifier.weight(1f)) {
                Text(device.label, color = p.text, style = MaterialTheme.typography.titleMedium)
                Text(device.hardwareId, color = p.faint, style = MaterialTheme.typography.bodySmall.copy(fontFamily = FontFamily.Monospace))
            }
            StatusBadge(if (device.online) "Online" else "Offline", if (device.online) Tone.GOOD else Tone.BAD)
        }
        FlowRow(horizontalArrangement = Arrangement.spacedBy(6.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
            StatusBadge("Firmware ${device.firmware.ifBlank { "?" }}")
            StatusBadge(when (device.display) { "oled" -> "OLED"; "epaper" -> "E-ink"; else -> "Display ?" })
            if (device.sessionCount > 0) StatusBadge("${device.sessionCount} phone links", Tone.INFO)
        }
        FlowRow(horizontalArrangement = Arrangement.spacedBy(6.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
            ToneButton("Rename", onRename)
            ToneButton("Forget", onForget, tone = Tone.WARN)
            ToneButton("Factory reset", onReset, tone = Tone.BAD)
        }
    }
}

@Composable
private fun TableAndAtlasCard(admin: AdminState, actions: AdminActions) {
    val p = palette
    var confirmReturn by remember { mutableStateOf(false) }
    var confirmFactory by remember { mutableStateOf(false) }
    var typed by remember { mutableStateOf("") }
    if (confirmReturn) {
        ConfirmDialog(
            "Return table to lobby?",
            "A match in progress ends as a draw and every player is removed from the table.",
            "Return to lobby",
            onConfirm = { confirmReturn = false; actions.run { resetTable() } },
            onDismiss = { confirmReturn = false },
        )
    }
    if (confirmFactory) {
        AlertDialog(
            onDismissRequest = { confirmFactory = false },
            containerColor = p.surface,
            title = { Text("Factory reset Atlas?") },
            text = {
                Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
                    Text(
                        "Atlas erases every profile, PIN, statistic it holds, Sigil pairing, the Wi-Fi password and all " +
                            "settings, then restarts as new. The microSD card is not erased. This cannot be undone.",
                        color = p.muted,
                    )
                    OutlinedTextField(value = typed, onValueChange = { typed = it.take(5) }, label = { Text("Type RESET to confirm") }, singleLine = true)
                }
            },
            confirmButton = {
                TextButton(
                    onClick = { confirmFactory = false; typed = ""; actions.run { factoryResetAtlas() } },
                    enabled = typed.trim().uppercase() == "RESET",
                ) { Text("Factory reset Atlas", color = p.bad, fontWeight = FontWeight.Bold) }
            },
            dismissButton = { TextButton(onClick = { confirmFactory = false; typed = "" }) { Text("Cancel", color = p.text) } },
        )
    }
    // Chosen here, sent only by Save (the saved values reset them on refresh).
    var pairingDraft by remember(admin.pairingWindowMs) { mutableStateOf(admin.pairingWindowMs) }
    var volumeDraft by remember(admin.speakerVolume) { mutableStateOf(admin.speakerVolume) }
    val tableDirty = pairingDraft != admin.pairingWindowMs || volumeDraft != admin.speakerVolume
    BrassCard {
        Eyebrow("Table and Atlas")
        if (admin.pairingChoicesMs.isNotEmpty()) {
            ChoiceDropdown(
                label = "Atlas pairing window",
                options = admin.pairingChoicesMs.map { it to "${it / 1000} seconds" },
                selected = pairingDraft ?: admin.pairingChoicesMs.first(),
                onSelect = { ms -> pairingDraft = ms },
            )
            Text(
                "How long Atlas listens after Pair a Sigil is tapped on its screen. A Sigil listens for 15 seconds, so with " +
                    "a longer window tap Pair on Atlas first, then press Pair on the Sigil.",
                color = p.faint,
                style = MaterialTheme.typography.bodySmall,
            )
        }
        volumeDraft?.let { volume ->
            ChoiceDropdown(
                label = "Atlas speaker volume",
                options = SPEAKER_LEVELS,
                selected = volume,
                onSelect = { v -> volumeDraft = v },
            )
            Text(
                "Atlas's speaker plays table-wide cues (countdown, turn changes, timer warnings, pause, wins) so players " +
                    "without a Sigil hear them too. Every cue also shows on screen.",
                color = p.faint,
                style = MaterialTheme.typography.bodySmall,
            )
        }
        if (admin.pairingChoicesMs.isNotEmpty() || admin.speakerVolume != null) {
            AccentButton(
                if (tableDirty) "Save Atlas settings" else "Atlas settings saved",
                {
                    val ms = pairingDraft.takeIf { it != admin.pairingWindowMs }
                    val v = volumeDraft.takeIf { it != admin.speakerVolume }
                    actions.run { saveTableSettings(ms, v); refresh(admin = true, gameMaster = false) }
                },
                Modifier.fillMaxWidth(),
                enabled = tableDirty && !admin.busy,
            )
        }
        ToneButton("Return table to lobby", { confirmReturn = true }, Modifier.fillMaxWidth(), tone = Tone.WARN)
        ToneButton("Factory reset Atlas", { confirmFactory = true }, Modifier.fillMaxWidth(), tone = Tone.BAD)
        Text(
            "Return to lobby ends a match in progress as a draw for everyone. Firmware updates stay on the portal's " +
                "Atlas firmware page.",
            color = p.faint,
            style = MaterialTheme.typography.bodySmall,
        )
    }
}

@Composable
private fun AccountsCard(admin: AdminState, avatars: List<AvatarIcon>, actions: AdminActions) {
    val p = palette
    BrassCard {
        Eyebrow("Account permissions") {
            TextButton(onClick = actions.onRefresh) { Text("Refresh", color = p.muted) }
        }
        Text(
            "Permissions can be combined. The initial Admin must remain an Admin. Privileged accounts require a PIN.",
            color = p.muted,
            style = MaterialTheme.typography.bodySmall,
        )
        admin.accounts.forEach { account -> AccountPermissionsRow(account, avatars, actions) }
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun AccountPermissionsRow(account: AccountInfo, avatars: List<AvatarIcon>, actions: AdminActions) {
    val p = palette
    var bits by remember(account.profileId, account.permissions) { mutableIntStateOf(account.permissions) }
    var confirmArchive by remember { mutableStateOf(false) }
    if (confirmArchive) {
        ConfirmDialog(
            if (account.archived) "Restore account?" else "Archive account?",
            if (account.archived) "Restore ${account.name} and their existing permissions?"
            else "${account.name}: sign-in and Sigil use will be blocked; statistics stay saved. The account must first leave the table.",
            if (account.archived) "Restore" else "Archive",
            onConfirm = {
                confirmArchive = false
                actions.run { archive(account.profileId, !account.archived); refresh(admin = true, gameMaster = false) }
            },
            onDismiss = { confirmArchive = false },
        )
    }
    Column(
        Modifier
            .fillMaxWidth()
            .clip(RoundedCornerShape(14.dp))
            .background(p.surface2)
            .border(1.dp, p.line, RoundedCornerShape(14.dp))
            .padding(12.dp),
        verticalArrangement = Arrangement.spacedBy(6.dp),
    ) {
        Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(10.dp)) {
            PlayerAvatar(account.name.ifBlank { account.profileId }, 0, avatars.firstOrNull { it.id == account.avatar }, size = 36.dp)
            Column(Modifier.weight(1f)) {
                Text(account.name.ifBlank { account.profileId } + if (account.archived) " · Archived" else "",
                    color = p.text, style = MaterialTheme.typography.titleSmall)
                Text(account.profileId, color = p.faint, style = MaterialTheme.typography.bodySmall.copy(fontFamily = FontFamily.Monospace))
            }
        }
        AccountPermission.entries.forEach { permission ->
            val on = bits and permission.bit != 0
            Row(
                Modifier
                    .fillMaxWidth()
                    .toggleable(value = on, role = Role.Checkbox) { bits = if (it) bits or permission.bit else bits and permission.bit.inv() },
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Checkbox(checked = on, onCheckedChange = null)
                Text(permission.label, color = p.text, style = MaterialTheme.typography.bodyMedium)
            }
        }
        FlowRow(horizontalArrangement = Arrangement.spacedBy(6.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
            AccentButton("Save permissions", {
                val b = bits
                actions.run { savePermissions(account.profileId, b); refresh(admin = true, gameMaster = false) }
            }, enabled = bits != account.permissions)
            ToneButton(if (account.archived) "Restore account" else "Archive account", { confirmArchive = true },
                tone = if (account.archived) Tone.NEUTRAL else Tone.BAD)
        }
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun GameMasterCard(info: SessionInfo, admin: AdminState, avatars: List<AvatarIcon>, actions: AdminActions) {
    val p = palette
    var pending by remember { mutableStateOf<Pair<AccountInfo, String>?>(null) }
    pending?.let { (account, action) ->
        val meaning = mapOf(
            "reset" to "Invalidate all browser sessions and suspend Sigil controls until this account signs in again? The seat and life totals stay.",
            "remove" to "Remove this player from the game and invalidate their connections?",
            "pass" to "Immediately pass this player's turn?",
            "mute" to "Block this account from sending future nudges?",
            "unmute" to "Allow this account to send future nudges?",
        )
        ConfirmDialog(
            "Game Master action",
            "${account.name}: ${meaning[action]}",
            "Apply",
            onConfirm = {
                pending = null
                actions.run { moderate(account.profileId, action); refresh(admin = false, gameMaster = true) }
            },
            onDismiss = { pending = null },
        )
    }
    BrassCard {
        Eyebrow("Game Master")
        admin.accounts.filter { !it.archived }.forEach { account ->
            Column(
                Modifier
                    .fillMaxWidth()
                    .clip(RoundedCornerShape(14.dp))
                    .background(p.surface2)
                    .border(1.dp, p.line, RoundedCornerShape(14.dp))
                    .padding(12.dp),
                verticalArrangement = Arrangement.spacedBy(8.dp),
            ) {
                Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                    PlayerAvatar(account.name.ifBlank { account.profileId }, 0, avatars.firstOrNull { it.id == account.avatar }, size = 36.dp)
                    Text(account.name.ifBlank { account.profileId }, color = p.text, style = MaterialTheme.typography.titleSmall)
                }
                FlowRow(horizontalArrangement = Arrangement.spacedBy(6.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
                    ToneButton("Force pass", { pending = account to "pass" })
                    ToneButton(if (account.nudgeMuted) "Unmute nudges" else "Mute nudges",
                        { pending = account to if (account.nudgeMuted) "unmute" else "mute" })
                    if (info.has(AccountPermission.GM_RESET_CONNECTIONS)) {
                        ToneButton("Reset connections", { pending = account to "reset" }, tone = Tone.WARN)
                    }
                    if (info.has(AccountPermission.GM_REMOVE_FROM_GAME)) {
                        ToneButton("Remove from game", { pending = account to "remove" }, tone = Tone.BAD)
                    }
                }
            }
        }
        Text(
            "Connection resets keep the seat and life totals. Removal ends active participation. Nudge mute is saved for " +
                "the future nudge feature.",
            color = p.faint,
            style = MaterialTheme.typography.bodySmall,
        )
    }
}

/** First-Admin setup, shown while Atlas has no Admin yet. */
@Composable
fun AdminSetupCard(actions: AdminActions) {
    val p = palette
    BrassCard(highlight = p.good) {
        Eyebrow("Set up this Atlas")
        Text(
            "Make your account the initial Admin. The Atlas screen shows a six-digit code; enter it here to prove you " +
                "are at the table.",
            color = p.muted,
        )
        AccentButton("Make my account the initial Admin", { actions.run { setupAdmin(); actions.onRefresh() } }, Modifier.fillMaxWidth())
    }
}
