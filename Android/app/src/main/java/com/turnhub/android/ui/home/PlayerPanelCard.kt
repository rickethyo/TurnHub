package com.turnhub.android.ui.home

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.FilterChip
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.semantics.LiveRegionMode
import androidx.compose.ui.semantics.liveRegion
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.unit.dp
import com.turnhub.android.data.PlayerSessionState
import com.turnhub.android.domain.TurnTimerStatus

/** Callbacks for [PlayerPanelCard]; each one sends at most one request to Atlas. */
data class PlayerPanelActions(
    val onPlayFromPhone: () -> Unit = {},
    val onJoin: () -> Unit = {},
    val onPass: () -> Unit = {},
    val onPauseResume: () -> Unit = {},
    val onTurnTimerChosen: (Long) -> Unit = {},
    val onSignOut: () -> Unit = {},
    val onFeedbackDismiss: () -> Unit = {},
)

/**
 * Sign in -> join -> PASS / pause-resume -> sign out, from this phone. Pure
 * function of [panel]: the buttons reflect Atlas's last snapshot and Atlas
 * decides every outcome.
 */
@Composable
fun PlayerPanelCard(panel: PlayerPanel, actions: PlayerPanelActions, modifier: Modifier = Modifier) {
    Card(modifier = modifier.fillMaxWidth()) {
        Column(modifier = Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
            Text("Play from this phone", style = MaterialTheme.typography.titleMedium)
            if (!panel.signedIn) {
                Text(
                    "Sign in to a profile to pass, pause and resume from this phone.",
                    style = MaterialTheme.typography.bodyMedium,
                )
                Button(onClick = actions.onPlayFromPhone) { Text("Sign in") }
            } else {
                Text(
                    panel.status,
                    style = MaterialTheme.typography.bodyLarge,
                    modifier = Modifier.semantics { liveRegion = LiveRegionMode.Polite },
                )
                ControlButtons(panel, actions)
                panel.timerEditor?.let { TurnTimerSetting(it, enabled = !panel.busy, onChosen = actions.onTurnTimerChosen) }
                TextButton(onClick = actions.onSignOut, enabled = !panel.busy) {
                    Text(signOutLabel(panel.session))
                }
            }
            panel.feedback?.let { feedback ->
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Text(
                        feedback.message,
                        style = MaterialTheme.typography.bodyMedium,
                        color = if (feedback.isError) MaterialTheme.colorScheme.error else MaterialTheme.colorScheme.onSurface,
                        modifier = Modifier
                            .weight(1f)
                            .semantics { liveRegion = if (feedback.isError) LiveRegionMode.Assertive else LiveRegionMode.Polite },
                    )
                    TextButton(onClick = actions.onFeedbackDismiss) { Text("Dismiss") }
                }
            }
        }
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun ControlButtons(panel: PlayerPanel, actions: PlayerPanelActions) {
    FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
        if (panel.canJoin) {
            Button(onClick = actions.onJoin) { Text("Join table") }
        } else {
            Button(onClick = actions.onPass, enabled = panel.canPass) { Text(panel.passLabel) }
            OutlinedButton(onClick = actions.onPauseResume, enabled = panel.canPauseResume) {
                Text(panel.pauseResumeLabel)
            }
        }
    }
}

/** Presets as chips plus a custom length; the choice is sent once, when saved. */
@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun TurnTimerSetting(editor: TurnTimerEditor, enabled: Boolean, onChosen: (Long) -> Unit) {
    var custom by rememberSaveable(editor.currentMs) { mutableStateOf(!editor.currentIsPreset) }
    var seconds by rememberSaveable(editor.currentMs) {
        mutableStateOf(if (editor.currentIsPreset) "90" else (editor.currentMs / 1000).toString())
    }
    val customMs = editor.customMs(seconds)

    Column(verticalArrangement = Arrangement.spacedBy(4.dp), modifier = Modifier.padding(top = 8.dp)) {
        Text("Turn timer (you are the host)", style = MaterialTheme.typography.titleSmall)
        Text(
            "Off shows a gentle cue after five minutes. A timer warns at ten seconds left; " +
                "running out never passes the turn.",
            style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
        FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            editor.presetsMs.forEach { ms ->
                FilterChip(
                    selected = !custom && editor.currentMs == ms,
                    onClick = { custom = false; if (ms != editor.currentMs) onChosen(ms) },
                    enabled = enabled,
                    label = { Text(TurnTimerStatus.settingLabel(ms)) },
                )
            }
            FilterChip(selected = custom, onClick = { custom = true }, enabled = enabled, label = { Text("Custom") })
        }
        if (custom) {
            Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                OutlinedTextField(
                    value = seconds,
                    onValueChange = { seconds = it.filter(Char::isDigit).take(4) },
                    singleLine = true,
                    enabled = enabled,
                    isError = customMs == null,
                    label = { Text("Seconds") },
                    supportingText = { Text("${editor.minMs / 1000} to ${editor.maxMs / 1000}") },
                    keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number),
                    modifier = Modifier.weight(1f),
                )
                Button(onClick = { customMs?.let(onChosen) }, enabled = enabled && customMs != null) { Text("Save") }
            }
        }
    }
}

private fun signOutLabel(session: PlayerSessionState): String =
    (session as? PlayerSessionState.SignedIn)?.name?.let { "Sign out ($it)" } ?: "Sign out"
