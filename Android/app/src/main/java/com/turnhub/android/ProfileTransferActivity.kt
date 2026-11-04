package com.turnhub.android

import android.content.ClipData
import android.content.Intent
import android.os.Bundle
import android.view.WindowManager
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.unit.dp
import androidx.core.content.FileProvider
import com.turnhub.android.data.ProfileSecret
import com.turnhub.android.standalone.LocalLibrary
import com.turnhub.android.standalone.PreferencesStandaloneStore
import com.turnhub.android.standalone.PreviewProfileTransfer
import com.turnhub.android.ui.theme.TurnHubTheme
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.json.JSONObject
import java.io.File
import java.util.UUID

/** The Play app owns consent and account unlocking. No caller can directly read its preferences. */
class ProfileTransferActivity : ComponentActivity() {
    /** The calling app's signing certificates; empty when unknown. */
    private fun callerSigners(caller: String?): Set<String> = runCatching {
        val pm = packageManager
        if (caller == null) return emptySet()
        if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.P) {
            val info = pm.getPackageInfo(caller, android.content.pm.PackageManager.GET_SIGNING_CERTIFICATES).signingInfo
                ?: return emptySet()
            // The APK's current signers only, never a rotated key's history.
            info.apkContentsSigners.orEmpty().map { PreviewProfileTransfer.sha256Hex(it.toByteArray()) }.toSet()
        } else {
            @Suppress("DEPRECATION")
            pm.getPackageInfo(caller, android.content.pm.PackageManager.GET_SIGNATURES).signatures.orEmpty()
                .map { PreviewProfileTransfer.sha256Hex(it.toByteArray()) }.toSet()
        }
    }.getOrDefault(emptySet())

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setResult(RESULT_CANCELED)
        if (!PreviewProfileTransfer.allowedCaller(BuildConfig.APPLICATION_ID, callingPackage, intent.action, callerSigners(callingPackage))) {
            finish(); return
        }
        window.addFlags(WindowManager.LayoutParams.FLAG_SECURE)
        // No tapjacking: ignore taps while another app draws over the consent screen.
        window.decorView.filterTouchesWhenObscured = true
        val library = runCatching {
            PreferencesStandaloneStore(this).loadLibrary()?.let { LocalLibrary.fromJson(JSONObject(it)) } ?: LocalLibrary()
        }.getOrNull()
        setContent { TurnHubTheme {
            val scope = rememberCoroutineScope()
            val selected = remember { mutableStateMapOf<String, Boolean>() }
            val secrets = remember { mutableStateMapOf<String, String>() }
            var busy by remember { mutableStateOf(false) }
            var error by remember { mutableStateOf<String?>(null) }
            Surface(Modifier.fillMaxSize()) {
                Column(Modifier.safeDrawingPadding().padding(24.dp).verticalScroll(rememberScrollState()),
                    verticalArrangement = Arrangement.spacedBy(16.dp)) {
                    Text("Copy accounts to TurnHub Preview", style = MaterialTheme.typography.headlineSmall)
                    Text("Choose accounts from TurnHub on this phone. Their identities, appearance, PIN protection, Atlas links and retained match history will be copied. TurnHub keeps its originals. Later changes stay separate.")
                    if (library == null) Text("TurnHub's saved profiles could not be read. Open TurnHub and check its storage message first.")
                    else if (library.players.isEmpty()) Text("There are no local accounts to copy. Profiles stored only on Atlas can be linked from Preview after connecting to Atlas.")
                    library?.players?.forEach { player ->
                        val checked = selected[player.localId] == true
                        Row {
                            Checkbox(checked, onCheckedChange = { if (!busy) { selected[player.localId] = it; secrets.remove(player.localId); error = null } }, enabled = !busy)
                            Column(Modifier.weight(1f)) {
                                Text(player.name, style = MaterialTheme.typography.titleMedium)
                                Text("ID ${player.profileId}" + if (player.localId == library.deviceAdmin) " · Device Admin" else "")
                            }
                        }
                        if (checked && player.credential != null) OutlinedTextField(
                            value = secrets[player.localId].orEmpty(),
                            onValueChange = { secrets[player.localId] = it.take(ProfileSecret.MAX_CHARS); error = null },
                            label = { Text("${player.name}'s PIN or password") },
                            visualTransformation = PasswordVisualTransformation(), singleLine = true,
                            enabled = !busy, modifier = Modifier.fillMaxWidth())
                    }
                    error?.let { Text(it, color = MaterialTheme.colorScheme.error) }
                    Button(onClick = {
                        val source = library ?: return@Button
                        val ids = selected.filterValues { it }.keys.toSet()
                        val enteredSecrets = secrets.toMap()
                        busy = true; error = null
                        scope.launch {
                            val result = runCatching { withContext(Dispatchers.IO) {
                                val bundle = PreviewProfileTransfer.export(source, ids, enteredSecrets)
                                val folder = File(cacheDir, "profile-transfer").apply { mkdirs() }
                                folder.listFiles()?.filter { it.isFile }?.forEach { deleteFile -> deleteFile.delete() }
                                val file = File(folder, "${UUID.randomUUID()}.json")
                                file.writeText(bundle.toString(), Charsets.UTF_8)
                                FileProvider.getUriForFile(this@ProfileTransferActivity, "$packageName.share", file)
                            } }
                            busy = false
                            result.onSuccess { uri ->
                                secrets.clear()
                                setResult(RESULT_OK, Intent().setDataAndType(uri, "application/json")
                                    .apply { clipData = ClipData.newRawUri("Selected TurnHub profiles", uri) }
                                    .addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION))
                                finish()
                            }.onFailure { error = it.message ?: "The selected accounts could not be copied." }
                        }
                    }, enabled = !busy && library != null && selected.any { it.value }, modifier = Modifier.fillMaxWidth()) {
                        Text(if (busy) "Preparing accounts…" else "Copy selected accounts to Preview")
                    }
                    TextButton(onClick = { if (!busy) finish() }, enabled = !busy, modifier = Modifier.fillMaxWidth()) { Text("Cancel") }
                }
            }
        } }
    }
}
