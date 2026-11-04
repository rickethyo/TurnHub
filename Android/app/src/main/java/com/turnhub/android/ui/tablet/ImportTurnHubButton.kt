package com.turnhub.android.ui.tablet

import android.app.Activity
import android.content.ComponentName
import android.content.Intent
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.semantics.LiveRegionMode
import androidx.compose.ui.semantics.liveRegion
import androidx.compose.ui.semantics.semantics
import com.turnhub.android.BuildConfig
import com.turnhub.android.standalone.PreviewProfileTransfer
import com.turnhub.android.standalone.StandaloneTable
import com.turnhub.android.ui.theme.palette
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.json.JSONObject

/** User-driven, same-phone import. Missing support in an older Play app is a normal recoverable state. */
@Composable
fun ImportTurnHubButton(table: StandaloneTable) {
    if (BuildConfig.APPLICATION_ID != PreviewProfileTransfer.PREVIEW) return
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    var busy by remember { mutableStateOf(false) }
    var message by remember { mutableStateOf<String?>(null) }
    val launcher = rememberLauncherForActivityResult(ActivityResultContracts.StartActivityForResult()) { result ->
        if (result.resultCode != Activity.RESULT_OK) { busy = false; return@rememberLauncherForActivityResult }
        val uri = result.data?.data
        scope.launch {
            val copied = runCatching {
                require(uri != null && uri.scheme == "content" && uri.authority == "${PreviewProfileTransfer.TURNHUB}.share" &&
                    uri.pathSegments.firstOrNull() == "profile_transfer")
                val bundle = withContext(Dispatchers.IO) {
                    requireNotNull(context.contentResolver.openInputStream(uri)).use { PreviewProfileTransfer.read(it) }
                }
                table.importPreviewProfiles(bundle)
            }.getOrNull()
            busy = false
            message = when (copied) {
                StandaloneTable.CopyResult.ADDED -> "Accounts and profiles copied from TurnHub. Later changes stay separate."
                StandaloneTable.CopyResult.ALREADY_HERE -> "Those accounts and their history are already in Preview."
                else -> "The accounts could not be imported. Existing Preview profiles were kept."
            }
        }
    }
    Column {
        TextButton(onClick = {
            message = null
            val intent = Intent(PreviewProfileTransfer.ACTION)
                .setComponent(ComponentName(PreviewProfileTransfer.TURNHUB, PreviewProfileTransfer.ACTIVITY))
            try { launcher.launch(intent); busy = true }
            catch (_: android.content.ActivityNotFoundException) {
                message = "Your installed TurnHub needs the account-transfer update. Update TurnHub in Google Play, then try again. You can also use Copy an account from another device."
            } catch (_: SecurityException) {
                message = "TurnHub could not open the account transfer. Update TurnHub in Google Play, then try again."
            }
        }, enabled = !busy && table.state.value.storageProblem == null, modifier = Modifier.fillMaxWidth()) {
            Text(if (busy) "Importing from TurnHub…" else "Import accounts from TurnHub")
        }
        message?.let { Text(it, color = palette.muted, modifier = Modifier.semantics { liveRegion = LiveRegionMode.Polite }) }
    }
}
