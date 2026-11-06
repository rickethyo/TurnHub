package com.turnhub.android.ui.tablet

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.semantics.LiveRegionMode
import androidx.compose.ui.semantics.liveRegion
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.dp
import androidx.compose.ui.text.style.TextOverflow
import com.turnhub.android.lan.LanUiState
import com.turnhub.android.lan.LanViewModel

/** A reserved status row outside play controls; details open only when requested. */
@Composable
internal fun RoomFeedback(state: LanUiState, model: LanViewModel) {
    if (state.remote == null && state.preview != null) return
    val uncertain = state.uncertain && !state.busy
    val message = state.message ?: state.actionNote ?: state.historyNote
    if (state.host == null && state.remote == null && message == null && !uncertain) return
    var details by remember { mutableStateOf(false) }
    val status = when {
        uncertain -> "Action needs confirmation"
        state.busy -> "Saving your change…"
        message != null -> message
        state.host != null -> "Hosting table · ${state.guests.size} ${if (state.guests.size == 1) "device" else "devices"} joined"
        state.online -> "Game host connected"
        else -> "Game host disconnected · reconnecting"
    }
    Row(Modifier.fillMaxWidth().height(48.dp).clickable { details = true }.padding(horizontal = 12.dp)
        .semantics { liveRegion = LiveRegionMode.Polite }, verticalAlignment = Alignment.CenterVertically) {
        Text(status, modifier = Modifier.weight(1f), style = MaterialTheme.typography.bodySmall,
            maxLines = 1, overflow = TextOverflow.Ellipsis)
        TextButton(onClick = { if (uncertain) model.retry() else details = true }) {
            Text(if (uncertain) "Retry last action" else "Details")
        }
    }
    if (details) AlertDialog(onDismissRequest = { details = false }, title = { Text("Table connection") },
        text = { Text(message ?: if (state.host != null) "This phone owns the game and saves its results. Other devices join this phone's table. Atlas has a separate connection for Sigils and syncing."
            else if (state.online) "This phone is connected to the game host. The host saves changes and results. Atlas connectivity is shown separately at the top of the app."
            else "The game host isn't answering. Keep this screen open to reconnect. Check the host phone's Wi-Fi and reopen TurnHub there.") },
        confirmButton = { TextButton(onClick = { details = false; if (uncertain) model.retry() else model.dismissMessage() }) {
            Text(if (uncertain) "Retry last action" else "Done")
        } },
        dismissButton = { TextButton(onClick = { details = false }) { Text("Close") } })
}
