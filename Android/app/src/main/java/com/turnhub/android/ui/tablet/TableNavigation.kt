package com.turnhub.android.ui.tablet

import androidx.compose.foundation.layout.*
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import com.turnhub.android.protocol.TableState
import com.turnhub.android.ui.components.ToneButton
import com.turnhub.android.ui.theme.palette

/** The embedded table has the same navigation regardless of its authority. */
@Composable
internal fun TableNavigation(
    state: TableState,
    onFullScreen: () -> Unit,
    practice: Boolean = false,
    action: (@Composable () -> Unit)? = null,
) {
    val p = palette
    Row(Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 4.dp),
        verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
        Column(Modifier.weight(1f)) {
            Text(when {
                practice -> "Practice table"
                state == TableState.LOBBY -> "Set up the table"
                state == TableState.GAME_OVER -> "Last game"
                state == TableState.PAUSED -> "Paused"
                else -> "Live game"
            }, color = p.text, style = MaterialTheme.typography.titleSmall)
            if (practice) Text("Try anything. Nothing here is saved.", color = p.muted, style = MaterialTheme.typography.bodySmall)
        }
        if (action != null) action() else ToneButton("Full screen", onFullScreen)
    }
}
