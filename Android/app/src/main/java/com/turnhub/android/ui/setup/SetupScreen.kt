package com.turnhub.android.ui.setup

import com.turnhub.android.data.FirmwareProduct
import com.turnhub.android.data.ProfileSecret
import android.Manifest
import android.content.pm.PackageManager
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.ui.draw.clip
import androidx.compose.ui.platform.LocalContext
import androidx.core.content.ContextCompat
import com.turnhub.android.data.presenceCodeFromQr
import com.turnhub.android.ui.components.QrScanner
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.selection.selectable
import androidx.compose.foundation.selection.selectableGroup
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.RadioButton
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.semantics.LiveRegionMode
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.liveRegion
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.text.input.VisualTransformation
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.turnhub.android.data.AtlasSetupAssistant
import com.turnhub.android.data.SetupState
import com.turnhub.android.data.SetupStep
import com.turnhub.android.data.UpdateProgress
import com.turnhub.android.data.UpdatesState
import com.turnhub.android.protocol.ProfileSummary
import com.turnhub.android.ui.components.AccentButton
import com.turnhub.android.ui.components.BrassCard
import com.turnhub.android.ui.components.Eyebrow
import com.turnhub.android.ui.components.ToneButton
import com.turnhub.android.ui.theme.palette
import kotlinx.coroutines.delay

/** Runs one assistant call on the ViewModel's scope; the non-suspending ones are plain callbacks. */
data class SetupActions(
    val run: (suspend AtlasSetupAssistant.() -> Unit) -> Unit = {},
    val dismiss: () -> Unit = {},
    val close: () -> Unit = {},
)

/**
 * First-run setup, one step per screen (FIRST_RUN_SETUP.md). Everything is
 * text and labeled controls; nothing depends on color. Atlas decides every
 * step; this only shows what [state] says.
 */
@Composable
fun SetupScreen(state: SetupState, actions: SetupActions, modifier: Modifier = Modifier) {
    val p = palette
    Column(modifier.fillMaxWidth(), verticalArrangement = Arrangement.spacedBy(14.dp)) {
        BrassCard(highlight = p.accent) {
            Eyebrow(
                if (state.updatesOnly) {
                    "Firmware updates"
                } else if (state.step.number in 1..SetupStep.COUNTED) {
                    "Set up this table · Step ${state.step.number} of ${SetupStep.COUNTED}"
                } else {
                    "Set up this table"
                },
            )
            Text(
                title(state.step),
                fontSize = 24.sp,
                fontWeight = FontWeight.Bold,
                color = p.text,
                modifier = Modifier.semantics { heading() },
            )
            state.error?.let {
                Text(
                    it,
                    color = p.bad,
                    fontWeight = FontWeight.SemiBold,
                    modifier = Modifier.semantics { liveRegion = LiveRegionMode.Assertive },
                )
            }
            state.note?.let {
                Text(it, color = p.muted, modifier = Modifier.semantics { liveRegion = LiveRegionMode.Polite })
            }
            if (state.busy) LinearProgressIndicator(Modifier.fillMaxWidth())
            when (state.step) {
                SetupStep.WELCOME -> Welcome(actions)
                SetupStep.ACCOUNT -> Account(state, actions)
                SetupStep.TABLE_CODE -> TableCode(state, actions)
                SetupStep.SIGILS -> Sigils(state, actions)
                SetupStep.UPDATES -> Updates(state, actions)
                SetupStep.WIFI -> Wifi(state, actions)
                SetupStep.RESTARTING -> Text(
                "Keep this screen open. The app rejoins the table's Wi-Fi by itself. " +
                    "If Android asks to connect to the table's Wi-Fi, choose Connect.",
                color = p.muted,
            )
                SetupStep.DONE -> Done(actions)
            }
        }
        // A new code can be needed on later steps too (after an update, or when one ran out).
        if (state.codeShowing && state.step != SetupStep.TABLE_CODE) {
            BrassCard(highlight = p.warn) {
                Eyebrow("Table code")
                CodeEntry(state, actions)
            }
        }
    }
}

private fun title(step: SetupStep) = when (step) {
    SetupStep.WELCOME -> "Welcome to TurnHub"
    SetupStep.ACCOUNT -> "Your account"
    SetupStep.TABLE_CODE -> "Confirm you're at the table"
    SetupStep.SIGILS -> "Pair your Sigils"
    SetupStep.UPDATES -> "Check for updates"
    SetupStep.WIFI -> "Secure the table's Wi-Fi"
    SetupStep.RESTARTING -> "Atlas is restarting"
    SetupStep.DONE -> "You're all set"
}

@Composable
private fun Welcome(actions: SetupActions) {
    val p = palette
    Text(
        "This Atlas is new. Setup takes about five minutes, and you need to stay at the table. You'll make an " +
            "account, confirm you're at the table, pair your Sigils, install any updates, and give the table its own Wi-Fi password.",
        color = p.muted,
    )
    AccentButton("Start", { actions.run { next() } }, Modifier.fillMaxWidth())
    ToneButton("Not now, just play", actions.dismiss, Modifier.fillMaxWidth())
}

@Composable
private fun Account(state: SetupState, actions: SetupActions) {
    val p = palette
    var existing by rememberSaveable { mutableStateOf(false) }
    var name by rememberSaveable { mutableStateOf("") }
    var pin by rememberSaveable { mutableStateOf("") }
    var confirm by rememberSaveable { mutableStateOf("") }
    var chosen by rememberSaveable { mutableStateOf<String?>(null) }
    if (!existing || state.profiles.isEmpty()) {
        Text("This account becomes the table's Admin. Your name and statistics stay with it.", color = p.muted)
        OutlinedTextField(name, { name = it.take(32) }, label = { Text("Your name") }, singleLine = true, modifier = Modifier.fillMaxWidth())
        PinField(pin, { pin = it }, "Choose a PIN (4–8 digits) or a password")
        PinField(confirm, { confirm = it }, "Type it again")
        val mismatch = confirm.isNotEmpty() && confirm != pin
        if (mismatch) Text("The PINs don't match.", color = p.bad)
        AccentButton(
            "Create account",
            { actions.run { createAccount(name, pin) } },
            Modifier.fillMaxWidth(),
            enabled = !state.busy && name.isNotBlank() && pin.isNotEmpty() && confirm == pin,
        )
        if (state.profiles.isNotEmpty()) {
            TextButton({ existing = true }) { Text("I already have an account on this Atlas") }
        }
    } else {
        Text("Choose your account.", color = p.muted)
        Column(Modifier.selectableGroup()) {
            state.profiles.forEach { profile ->
                Row(
                    Modifier
                        .fillMaxWidth()
                        .selectable(chosen == profile.profileId, role = Role.RadioButton) { chosen = profile.profileId }
                        .padding(vertical = 6.dp),
                    verticalAlignment = Alignment.CenterVertically,
                ) {
                    RadioButton(chosen == profile.profileId, onClick = null)
                    Text(profile.name.ifBlank { profile.profileId }, color = p.text, modifier = Modifier.padding(start = 8.dp))
                }
            }
        }
        PinField(pin, { pin = it }, "PIN or password")
        val profile: ProfileSummary? = state.profiles.firstOrNull { it.profileId == chosen }
        AccentButton(
            "Sign in",
            { profile?.let { actions.run { signIn(it, pin) } } },
            Modifier.fillMaxWidth(),
            enabled = !state.busy && profile != null && pin.isNotEmpty(),
        )
        TextButton({ existing = false }) { Text("Create a new account instead") }
    }
}

@Composable
private fun TableCode(state: SetupState, actions: SetupActions) {
    val p = palette
    Text(
        "Only someone at the table can set it up. Scan or type the code on the Atlas screen " +
            "to make your account this table's Admin.",
        color = p.muted,
    )
    CodeEntry(state, actions)
}

@Composable
private fun CodeEntry(state: SetupState, actions: SetupActions) {
    val p = palette
    var code by rememberSaveable { mutableStateOf("") }
    if (!state.codeShowing) {
        AccentButton("Show a code on Atlas", { actions.run { requestCode() } }, Modifier.fillMaxWidth(), enabled = !state.busy)
        return
    }
    val context = LocalContext.current
    var scanning by rememberSaveable { mutableStateOf(false) }
    var scanHint by rememberSaveable { mutableStateOf<String?>(null) }
    val cameraPermission = rememberLauncherForActivityResult(ActivityResultContracts.RequestPermission()) { granted ->
        scanning = granted
        if (!granted) scanHint = "Camera permission is off. Type the code instead."
    }
    Text("Atlas is showing a six-digit code and a QR code. Scan the QR or type the digits. They stay up for 90 seconds.", color = p.muted)
    if (scanning) {
        QrScanner(
            onScanned = { text ->
                val scanned = presenceCodeFromQr(text)
                if (scanned != null) {
                    scanning = false
                    actions.run { confirmCode(scanned) }
                } else {
                    scanHint = "That isn't the table code. Scan the QR next to the six digits."
                }
            },
            modifier = Modifier.fillMaxWidth().height(240.dp).clip(RoundedCornerShape(12.dp)),
        )
        ToneButton("Stop scanning", { scanning = false }, Modifier.fillMaxWidth())
    } else {
        ToneButton(
            "Scan the QR code",
            {
                scanHint = null
                if (ContextCompat.checkSelfPermission(context, Manifest.permission.CAMERA) == PackageManager.PERMISSION_GRANTED) {
                    scanning = true
                } else {
                    cameraPermission.launch(Manifest.permission.CAMERA)
                }
            },
            Modifier.fillMaxWidth(),
            enabled = !state.busy,
        )
    }
    scanHint?.let { Text(it, color = p.muted) }
    OutlinedTextField(
        code,
        { code = it.filter(Char::isDigit).take(6) },
        label = { Text("Code from the Atlas screen") },
        singleLine = true,
        keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.NumberPassword),
        modifier = Modifier.fillMaxWidth(),
    )
    AccentButton(
        "Confirm code",
        { actions.run { confirmCode(code) }; code = "" },
        Modifier.fillMaxWidth(),
        enabled = !state.busy && code.length == 6,
    )
    TextButton({ actions.run { requestCode() } }) { Text("Show a new code") }
}

@Composable
private fun Sigils(state: SetupState, actions: SetupActions) {
    val p = palette
    LaunchedEffect(Unit) {
        while (true) {
            actions.run { refreshSigils() }
            delay(3_000)
        }
    }
    Text(
        "Pair your Sigils now so one update covers every device.\n" +
            "1. On the Atlas screen, tap Pair a Sigil.\n" +
            "2. On the Sigil, start pairing from the menu.\n" +
            "3. If both screens show the same code, tap Codes match on Atlas.",
        color = p.muted,
    )
    if (state.sigils.isEmpty()) {
        Text("No Sigils paired yet.", color = p.text, modifier = Modifier.semantics { liveRegion = LiveRegionMode.Polite })
    } else {
        Column(Modifier.semantics { liveRegion = LiveRegionMode.Polite }, verticalArrangement = Arrangement.spacedBy(4.dp)) {
            Text("Paired:", color = p.text, fontWeight = FontWeight.SemiBold)
            state.sigils.forEach { s ->
                val display = when (s.display) { "oled" -> "OLED"; "epaper" -> "E-ink"; else -> "display unknown" }
                Text("• ${s.label} ($display, firmware ${s.firmware}${if (s.online) "" else ", offline"})", color = p.text)
            }
        }
    }
    if (state.sigils.isEmpty()) {
        AccentButton("I have no Sigils yet", { actions.run { sigilsDone() } }, Modifier.fillMaxWidth())
    } else {
        AccentButton("Done pairing", { actions.run { sigilsDone() } }, Modifier.fillMaxWidth())
    }
}

@Composable
private fun Updates(state: SetupState, actions: SetupActions) {
    val p = palette
    when (val u = state.updates) {
        UpdatesState.NotChecked, UpdatesState.Checking -> Text("Checking GitHub for the latest release…", color = p.muted)
        is UpdatesState.Unavailable -> {
            Text(u.reason, color = p.muted)
            if (state.updatesOnly) {
                ToneButton("Try again", { actions.run { openUpdates() } }, Modifier.fillMaxWidth())
                AccentButton("Close", actions.close, Modifier.fillMaxWidth())
            } else {
                Text("You can carry on and update later from Settings.", color = p.muted)
                ToneButton("Try again", { actions.run { checkUpdates() } }, Modifier.fillMaxWidth())
                AccentButton("Continue", { actions.run { skipUpdates(); next() } }, Modifier.fillMaxWidth())
            }
        }
        is UpdatesState.Ready -> {
            Text("Latest release: ${u.release}", color = p.muted)
            u.plan.targets.forEach { t ->
                // A card with no pack yet has nothing running; that isn't "unknown".
                val running = t.running?.toString() ?: if (t.product == FirmwareProduct.PORTAL) "not installed" else "unknown"
                val line = when {
                    t.available == null -> "${t.label}: $running, no update in this release"
                    t.needsUpdate -> "${t.label}: $running → ${t.available.version}"
                    else -> "${t.label}: $running, up to date"
                }
                Text("• $line", color = p.text)
            }
            if (u.plan.anyUpdate) {
                Text(
                    (if (u.plan.pending.any { it.product == FirmwareProduct.PORTAL }) {
                        "The web portal installs first, without a restart. Atlas updates next, then each Sigil. "
                    } else {
                        "Atlas updates first, then each Sigil. "
                    }) +
                        "Atlas restarts once and the app reconnects on its own, " +
                        "then asks for one more table code. Allow about a minute per device.",
                    color = p.muted,
                )
                AccentButton("Install all updates (recommended)", { actions.run { installUpdates() } }, Modifier.fillMaxWidth(), enabled = !state.busy)
                if (state.updatesOnly) {
                    ToneButton("Later", actions.close, Modifier.fillMaxWidth())
                } else {
                    ToneButton("Later", { actions.run { skipUpdates(); next() } }, Modifier.fillMaxWidth())
                }
            } else {
                Text("Everything is up to date.", color = p.text)
                if (state.updatesOnly) {
                    AccentButton("Close", actions.close, Modifier.fillMaxWidth())
                } else {
                    AccentButton("Continue", { actions.run { skipUpdates(); next() } }, Modifier.fillMaxWidth())
                }
            }
        }
        is UpdatesState.Installing -> {
            Text(u.detail, color = p.text, modifier = Modifier.semantics { liveRegion = LiveRegionMode.Polite })
            ProgressLines(u.lines)
        }
        is UpdatesState.Finished -> {
            ProgressLines(u.lines)
            if (u.lines.any { it.failed }) {
                Text("Anything that didn't update can be updated later from the Update available banner. The table plays either way.", color = p.muted)
            }
            if (state.updatesOnly) {
                AccentButton("Close", actions.close, Modifier.fillMaxWidth())
            } else {
                AccentButton("Continue", { actions.run { next() } }, Modifier.fillMaxWidth())
            }
        }
    }
}

@Composable
private fun ProgressLines(lines: List<UpdateProgress>) {
    val p = palette
    lines.forEach { line ->
        val mark = when {
            line.done -> "Done"
            line.failed -> "Not updated"
            else -> "…"
        }
        Text("• ${line.label}: ${line.state} ($mark)", color = if (line.failed) p.bad else p.text)
    }
}

@Composable
private fun Wifi(state: SetupState, actions: SetupActions) {
    val p = palette
    var password by rememberSaveable { mutableStateOf("") }
    var confirm by rememberSaveable { mutableStateOf("") }
    var show by rememberSaveable { mutableStateOf(false) }
    val ssid = state.status?.ssid?.ifBlank { null } ?: "TurnHub-Atlas"
    Text(
        "$ssid still uses the setup password, which anyone can look up. Choose a password of your own " +
            "(8 to 63 characters). Atlas restarts with the new password and this app rejoins on its own. " +
            "Other phones will need the new password.",
        color = p.muted,
    )
    val transform = if (show) VisualTransformation.None else PasswordVisualTransformation()
    OutlinedTextField(
        password, { password = it.take(63) }, label = { Text("New Wi-Fi password") }, singleLine = true,
        visualTransformation = transform, keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Password),
        trailingIcon = { TextButton({ show = !show }) { Text(if (show) "Hide" else "Show") } },
        modifier = Modifier.fillMaxWidth(),
    )
    OutlinedTextField(
        confirm, { confirm = it.take(63) }, label = { Text("Type it again") }, singleLine = true,
        visualTransformation = transform, keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Password),
        modifier = Modifier.fillMaxWidth(),
    )
    if (confirm.isNotEmpty() && confirm != password) Text("The passwords don't match.", color = p.bad)
    if (password.isNotEmpty() && password.length < 8) Text("At least 8 characters.", color = p.muted)
    AccentButton(
        "Finish setup",
        { actions.run { finish(password) } },
        Modifier.fillMaxWidth(),
        enabled = !state.busy && password.length >= 8 && confirm == password,
    )
}

@Composable
private fun Done(actions: SetupActions) {
    val p = palette
    Text(
        "The table is ready to play. Players join from a Sigil's menu or from this app. " +
            "To add a Sigil later, tap Menu, then Pair a Sigil, on the Atlas screen.",
        color = p.muted,
    )
    AccentButton("Go to the table", actions.close, Modifier.fillMaxWidth())
}

@Composable
private fun PinField(value: String, onChange: (String) -> Unit, label: String) {
    OutlinedTextField(
        value,
        { onChange(it.take(ProfileSecret.MAX_CHARS)) },
        label = { Text(label) },
        singleLine = true,
        visualTransformation = PasswordVisualTransformation(),
        keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Password),
        modifier = Modifier.fillMaxWidth(),
    )
}
