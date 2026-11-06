package com.turnhub.android.ui.tablet

import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Snackbar
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.semantics.LiveRegionMode
import androidx.compose.ui.semantics.liveRegion
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.dp
import com.turnhub.android.lan.LanUiState
import com.turnhub.android.lan.LanViewModel

/** Shared-room feedback overlays content; a retry never changes the game tab's geometry. */
@Composable
internal fun RoomFeedback(state: LanUiState, model: LanViewModel) {
    if (state.remote == null && state.preview != null) return
    val uncertain = state.uncertain && !state.busy
    val message = state.message ?: if (uncertain) "The host hasn't confirmed this action. Retry to check its saved result." else state.historyNote ?: return
    Snackbar(Modifier.padding(12.dp).semantics { liveRegion = LiveRegionMode.Polite }, actionOnNewLine = uncertain, action = {
        if (uncertain) TextButton(model::retry) { Text("Retry last action") }
        else TextButton(model::dismissMessage) { Text("Dismiss") }
    }) { Text(message, maxLines = 4) }
}
