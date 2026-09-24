package com.turnhub.android.ui.home

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.semantics.LiveRegionMode
import androidx.compose.ui.semantics.liveRegion
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.text.input.VisualTransformation
import androidx.compose.ui.unit.dp

/**
 * Asks for the Atlas Wi-Fi details when TurnHub can't join on its own (no
 * saved password worked). The network name is editable so the same prompt
 * serves renamed or setup networks. "I've joined it already" keeps the manual
 * path: the phone is on the Atlas Wi-Fi via Android settings.
 */
@Composable
fun WifiPasswordDialog(
    prompt: WifiPrompt,
    onSubmit: (ssid: String, passphrase: String) -> Unit,
    onUseCurrentWifi: () -> Unit,
    onDismiss: () -> Unit,
) {
    var ssid by rememberSaveable(prompt) { mutableStateOf(prompt.ssid) }
    var passphrase by rememberSaveable(prompt) { mutableStateOf("") }
    var showPassphrase by rememberSaveable(prompt) { mutableStateOf(false) }

    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text("Atlas Wi-Fi") },
        text = {
            Column(verticalArrangement = Arrangement.spacedBy(12.dp)) {
                Text(prompt.message, modifier = Modifier.semantics { liveRegion = LiveRegionMode.Polite })
                OutlinedTextField(
                    value = ssid,
                    onValueChange = { ssid = it },
                    singleLine = true,
                    label = { Text("Network name") },
                    modifier = Modifier.fillMaxWidth(),
                )
                OutlinedTextField(
                    value = passphrase,
                    onValueChange = { passphrase = it },
                    singleLine = true,
                    label = { Text("Wi-Fi password") },
                    supportingText = { Text("Shown in the Atlas portal under Wi-Fi security.") },
                    visualTransformation = if (showPassphrase) VisualTransformation.None else PasswordVisualTransformation(),
                    trailingIcon = {
                        TextButton(onClick = { showPassphrase = !showPassphrase }) {
                            Text(if (showPassphrase) "Hide" else "Show")
                        }
                    },
                    keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Password, imeAction = ImeAction.Go),
                    keyboardActions = KeyboardActions(onGo = { onSubmit(ssid, passphrase) }),
                    modifier = Modifier.fillMaxWidth(),
                )
                TextButton(onClick = onUseCurrentWifi) { Text("I've joined this Wi-Fi already") }
            }
        },
        confirmButton = { TextButton(onClick = { onSubmit(ssid, passphrase) }) { Text("Join") } },
        dismissButton = { TextButton(onClick = onDismiss) { Text("Cancel") } },
    )
}
