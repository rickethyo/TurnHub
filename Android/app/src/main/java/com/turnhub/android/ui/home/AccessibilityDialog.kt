package com.turnhub.android.ui.home

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.selection.selectable
import androidx.compose.foundation.selection.selectableGroup
import androidx.compose.foundation.selection.toggleable
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.RadioButton
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.semantics.LiveRegionMode
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.liveRegion
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.stateDescription
import androidx.compose.ui.unit.dp
import com.turnhub.android.protocol.AccessibilitySettings
import com.turnhub.android.protocol.LedStyle

/**
 * The signed-in player's Sigil accessibility, stored by Atlas with the
 * profile: Sigil sound, light style and Action hold times. The same choices
 * are in the portal's My Account; Atlas validates every save.
 */
@Composable
fun AccessibilityDialog(
    prompt: AccessibilityPrompt,
    onSave: (sigilSound: Boolean, ledStyle: LedStyle, longPressMs: Int, winHoldMs: Int) -> Unit,
    onDismiss: () -> Unit,
) {
    val settings = prompt.settings
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text("Sigil accessibility") },
        text = {
            if (settings == null) {
                Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                    if (prompt.error == null) {
                        Row(verticalAlignment = Alignment.CenterVertically) {
                            CircularProgressIndicator(modifier = Modifier.padding(end = 12.dp))
                            Text("Reading your settings from Atlas…")
                        }
                    }
                    ErrorText(prompt.error)
                }
            } else {
                Editor(settings, prompt, onSave)
            }
        },
        confirmButton = {},
        dismissButton = { TextButton(onClick = onDismiss) { Text(if (settings == null) "Close" else "Cancel") } },
    )
}

@Composable
private fun Editor(
    settings: AccessibilitySettings,
    prompt: AccessibilityPrompt,
    onSave: (Boolean, LedStyle, Int, Int) -> Unit,
) {
    // Drafts restart whenever Atlas reports different saved values.
    var sound by rememberSaveable(settings) { mutableStateOf(settings.sigilSound) }
    var style by rememberSaveable(settings) { mutableStateOf(settings.ledStyle) }
    var longMs by rememberSaveable(settings) { mutableIntStateOf(settings.longPressMs) }
    var winMs by rememberSaveable(settings) { mutableIntStateOf(settings.winHoldMs) }
    val limits = settings.limits
    val valid = limits.allows(longMs, winMs)

    Column(
        verticalArrangement = Arrangement.spacedBy(8.dp),
        modifier = Modifier.verticalScroll(rememberScrollState()),
    ) {
        Text(
            "Saved with your profile on Atlas and used at whichever Sigil you sit at.",
            style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
        if (!settings.stored) {
            Text(
                "Your saved settings could not be read, so defaults are shown. Saving replaces them.",
                style = MaterialTheme.typography.bodySmall,
            )
        }
        Row(
            verticalAlignment = Alignment.CenterVertically,
            modifier = Modifier
                .fillMaxWidth()
                .toggleable(value = sound, enabled = !prompt.busy, role = Role.Switch, onValueChange = { sound = it })
                .padding(vertical = 4.dp),
        ) {
            Column(Modifier.weight(1f)) {
                Text("Sigil sound")
                Text(
                    "Every tone also shows as text. A shared Sigil stays quiet if either player turns sound off.",
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
            Switch(checked = sound, onCheckedChange = null, enabled = !prompt.busy)
        }

        Text("Sigil lights", style = MaterialTheme.typography.titleSmall, modifier = Modifier.semantics { heading() })
        Column(Modifier.selectableGroup()) {
            LedStyle.entries.forEach { option ->
                Row(
                    verticalAlignment = Alignment.CenterVertically,
                    modifier = Modifier
                        .fillMaxWidth()
                        .selectable(
                            selected = option == style,
                            enabled = !prompt.busy,
                            role = Role.RadioButton,
                            onClick = { style = option },
                        )
                        .padding(vertical = 4.dp),
                ) {
                    RadioButton(selected = option == style, onClick = null, enabled = !prompt.busy)
                    Column(Modifier.padding(start = 8.dp)) {
                        Text(option.label)
                        Text(
                            option.description,
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant,
                        )
                    }
                }
            }
        }

        HoldStepper(
            title = "Hold Action to pause",
            valueMs = longMs,
            choices = limits.longPressChoices(),
            enabled = !prompt.busy,
            onChange = { longMs = it },
        )
        HoldStepper(
            title = "Hold Action to claim a win",
            valueMs = winMs,
            choices = limits.winHoldChoices(),
            enabled = !prompt.busy,
            onChange = { winMs = it },
        )
        Text(
            if (valid) {
                "On a shared Sigil the longer times apply. Needs Sigil firmware 0.5.4 or newer; " +
                    "older Sigils keep 2 and 5 seconds. You can always pause here instead."
            } else {
                "The win hold must be at least ${limits.minGapMs / 1000} second longer than the pause hold."
            },
            style = MaterialTheme.typography.bodySmall,
            color = if (valid) MaterialTheme.colorScheme.onSurfaceVariant else MaterialTheme.colorScheme.error,
            modifier = Modifier.semantics { liveRegion = LiveRegionMode.Polite },
        )
        ErrorText(prompt.error)
        OutlinedButton(
            onClick = { onSave(sound, style, longMs, winMs) },
            enabled = valid && !prompt.busy,
            modifier = Modifier.fillMaxWidth(),
        ) {
            Text(if (prompt.busy) "Saving…" else "Save on Atlas")
        }
    }
}

/** Shorter/longer buttons around a spoken value, instead of a drag-only slider. */
@Composable
private fun HoldStepper(title: String, valueMs: Int, choices: List<Int>, enabled: Boolean, onChange: (Int) -> Unit) {
    val index = choices.indexOf(valueMs).coerceAtLeast(0)
    val seconds = secondsLabel(valueMs)
    Column {
        Text(title, style = MaterialTheme.typography.titleSmall, modifier = Modifier.semantics { heading() })
        Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(12.dp)) {
            OutlinedButton(
                onClick = { onChange(choices[index - 1]) },
                enabled = enabled && index > 0,
                modifier = Modifier.semantics { contentDescription = "Shorter: $title" },
            ) { Text("−") }
            Text(seconds, modifier = Modifier.semantics { stateDescription = seconds })
            OutlinedButton(
                onClick = { onChange(choices[index + 1]) },
                enabled = enabled && index < choices.lastIndex,
                modifier = Modifier.semantics { contentDescription = "Longer: $title" },
            ) { Text("+") }
        }
    }
}

@Composable
private fun ErrorText(error: String?) {
    error?.let {
        Text(
            it,
            color = MaterialTheme.colorScheme.error,
            modifier = Modifier.semantics { liveRegion = LiveRegionMode.Assertive },
        )
    }
}

internal fun secondsLabel(ms: Int): String {
    val whole = ms / 1000
    val fraction = ms % 1000
    val text = if (fraction == 0) "$whole" else "$whole.${(fraction / 10).toString().padStart(2, '0').trimEnd('0')}"
    return "$text second" + if (ms == 1000) "" else "s"
}
