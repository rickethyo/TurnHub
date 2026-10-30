package com.turnhub.android.ui.tablet

import android.Manifest
import android.content.pm.PackageManager
import android.os.Build
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.semantics.LiveRegionMode
import androidx.compose.ui.semantics.liveRegion
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.unit.dp
import androidx.core.content.ContextCompat
import com.turnhub.android.data.ProfileSecret
import com.turnhub.android.lan.AccountCopyHost
import com.turnhub.android.standalone.AccountCopy
import com.turnhub.android.standalone.SavedLocalPlayer
import com.turnhub.android.standalone.StandaloneTable
import com.turnhub.android.ui.components.QrScanner
import com.turnhub.android.ui.theme.palette
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.json.JSONObject

/** Runs [action] once Android 17's Nearby devices (local network) permission is granted. */
@Composable
private fun rememberLocalNetworkAccess(onDenied: () -> Unit): (() -> Unit) -> Unit {
    val context = LocalContext.current
    var pending by remember { mutableStateOf<(() -> Unit)?>(null) }
    val launcher = rememberLauncherForActivityResult(ActivityResultContracts.RequestPermission()) { granted ->
        val action = pending
        pending = null
        if (granted) action?.invoke() else onDenied()
    }
    return { action ->
        if (Build.VERSION.SDK_INT >= 37 &&
            ContextCompat.checkSelfPermission(context, Manifest.permission.ACCESS_LOCAL_NETWORK) != PackageManager.PERMISSION_GRANTED) {
            pending = action
            launcher.launch(Manifest.permission.ACCESS_LOCAL_NETWORK)
        } else action()
    }
}

/**
 * "Use on another device" (owner, 2026-10-29), on the device that has the account: shows a QR
 * code that another of the owner's phones or tablets scans on the same Wi-Fi. One scan, then the
 * code is spent. The caller has unlocked the account.
 */
@Composable
fun ShareAccountDialog(player: SavedLocalPlayer, table: StandaloneTable, onClose: () -> Unit) {
    val p = palette
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    var invitation by remember { mutableStateOf<String?>(null) }
    var message by remember { mutableStateOf<String?>("Opening the link on this Wi-Fi…") }
    var host by remember { mutableStateOf<AccountCopyHost?>(null) }
    val withNetwork = rememberLocalNetworkAccess { message = "Allow Nearby devices in Android settings to share over Wi-Fi." }
    LaunchedEffect(player.localId) {
        withNetwork {
            scope.launch {
                val account = table.accountCopy(player.localId)
                val opened = runCatching { withContext(Dispatchers.IO) {
                    AccountCopyHost.open(context, account) {
                        scope.launch { invitation = null; message = "${player.name} was copied to the other device." }
                    }
                } }.getOrNull()
                if (opened == null) message = "Couldn't open the link. Join a Wi-Fi network on both devices, then try again."
                else { host = opened; invitation = opened.invitation; message = null }
            }
        }
    }
    DisposableEffect(Unit) { onDispose { host?.close() } }
    AlertDialog(
        onDismissRequest = onClose,
        containerColor = p.surface,
        title = { Text("Use ${player.name} on another device") },
        text = {
            Column(Modifier.verticalScroll(rememberScrollState()), verticalArrangement = Arrangement.spacedBy(12.dp)) {
                Text("On your other phone or tablet, on this same Wi-Fi, choose Use my account from another device " +
                    "(or Players, Copy from another device) and scan this code. It copies the account's ID, name, look " +
                    "and PIN. Each device keeps its own games; an Atlas adds them up.", color = p.muted)
                invitation?.let { InviteQr(it, "Code that copies ${player.name} to another device. Scan it with TurnHub.") }
                message?.let { Text(it, color = p.text, modifier = Modifier.semantics { liveRegion = LiveRegionMode.Polite }) }
            }
        },
        confirmButton = { TextButton(onClose) { Text("Done") } },
    )
}

/**
 * "Use my account from another device", on the device receiving it: scan the other device's
 * code, confirm the account's PIN or password, and add it. [admin] makes it this device's Admin.
 */
@Composable
fun CopyAccountDialog(table: StandaloneTable, admin: Boolean, onClose: () -> Unit) {
    val p = palette
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    var scanning by remember { mutableStateOf(false) }
    var busy by remember { mutableStateOf(false) }
    var message by remember { mutableStateOf<String?>(null) }
    var copied by remember { mutableStateOf<JSONObject?>(null) }
    var secret by remember { mutableStateOf("") }
    val camera = rememberLauncherForActivityResult(ActivityResultContracts.RequestPermission()) { granted ->
        scanning = granted
        if (!granted) message = "Allow the camera to scan the code."
    }
    val withNetwork = rememberLocalNetworkAccess { message = "Allow Nearby devices in Android settings to copy over Wi-Fi." }
    fun scan() {
        message = null
        if (ContextCompat.checkSelfPermission(context, Manifest.permission.CAMERA) == PackageManager.PERMISSION_GRANTED) scanning = true
        else camera.launch(Manifest.permission.CAMERA)
    }
    fun finish(account: JSONObject) {
        when (table.addCopiedAccount(account, admin)) {
            StandaloneTable.CopyResult.ADDED -> onClose()
            StandaloneTable.CopyResult.ALREADY_HERE -> message = "That account is already on this device."
            StandaloneTable.CopyResult.NOT_SAVED -> message = "Couldn't save the account. Please try again."
        }
    }
    fun fetch(text: String) = withNetwork {
        busy = true; message = "Copying…"
        scope.launch {
            val result = runCatching { withContext(Dispatchers.IO) { AccountCopyHost.fetch(context, text) } }
            busy = false
            result.onFailure { message = (it as? com.turnhub.android.lan.AccountCopyRefused)?.message ?: "Couldn't reach the other device. Check both are on the same Wi-Fi." }
            result.onSuccess { account ->
                message = null
                if (account.isNull("credential")) finish(account) else copied = account
            }
        }
    }
    val pending = copied
    AlertDialog(
        onDismissRequest = { if (!busy) onClose() },
        containerColor = p.surface,
        title = { Text("Use my account from another device") },
        text = {
            Column(Modifier.verticalScroll(rememberScrollState()), verticalArrangement = Arrangement.spacedBy(12.dp)) {
                when {
                    pending != null -> {
                        val name = pending.optString("name")
                        Text("Enter $name's PIN or password to finish.", color = p.muted)
                        OutlinedTextField(secret, { secret = it.take(ProfileSecret.MAX_CHARS) }, label = { Text("PIN or password") },
                            enabled = !busy, singleLine = true, visualTransformation = PasswordVisualTransformation(),
                            modifier = Modifier.fillMaxWidth())
                    }
                    scanning -> QrScanner({ text ->
                        if (AccountCopyHost.isInvitation(text)) { scanning = false; fetch(text) }
                    }, Modifier.fillMaxWidth().height(300.dp))
                    else -> Text("On the device that has your account, open Players, Edit your account, then " +
                        "Use on another device. Both devices need the same Wi-Fi.", color = p.muted)
                }
                message?.let { Text(it, color = p.text, modifier = Modifier.semantics { liveRegion = LiveRegionMode.Polite }) }
            }
        },
        confirmButton = {
            if (pending != null) TextButton(enabled = !busy && ProfileSecret.isValid(secret), onClick = {
                busy = true
                scope.launch {
                    val ok = withContext(Dispatchers.Default) {
                        runCatching { AccountCopy.fromJson(pending, "check").credential?.verifies(secret) == true }.getOrDefault(false)
                    }
                    busy = false; secret = ""
                    if (ok) finish(pending) else message = "PIN or password was not accepted."
                }
            }) { Text("Add account") }
            else if (!scanning) TextButton(::scan, enabled = !busy) { Text("Scan the code") }
        },
        dismissButton = { TextButton(onClose, enabled = !busy) { Text("Cancel") } },
    )
}
