package com.turnhub.android.ui.home

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.selection.selectable
import androidx.compose.foundation.selection.selectableGroup
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.RadioButton
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
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.liveRegion
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.unit.dp
import com.turnhub.android.protocol.ProfileSummary

/**
 * Picks an Atlas profile and takes its PIN. Profiles without a PIN can only
 * sign in physically, so they are listed but not selectable here. Atlas checks
 * the PIN (and throttles guesses); the app only checks it is 4-8 digits.
 */
@Composable
fun SignInDialog(
    prompt: SignInPrompt,
    onSubmit: (profile: ProfileSummary, pin: String) -> Unit,
    onDismiss: () -> Unit,
) {
    var selectedId by rememberSaveable { mutableStateOf<String?>(null) }
    var pin by rememberSaveable { mutableStateOf("") }
    val selected = prompt.profiles.firstOrNull { it.profileId == selectedId && it.hasPin }
    val submit = { selected?.let { onSubmit(it, pin) } }

    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text("Play from this phone") },
        text = {
            Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                when {
                    prompt.loading -> Row(verticalAlignment = Alignment.CenterVertically) {
                        CircularProgressIndicator(modifier = Modifier.padding(end = 12.dp))
                        Text("Loading profiles…")
                    }
                    prompt.profiles.isEmpty() && prompt.error == null ->
                        Text("No profiles yet. Create one in the Atlas portal first.")
                    else -> Column(Modifier.selectableGroup()) {
                        prompt.profiles.forEach { profile ->
                            val enabled = profile.hasPin && !prompt.submitting
                            Row(
                                verticalAlignment = Alignment.CenterVertically,
                                modifier = Modifier
                                    .fillMaxWidth()
                                    .selectable(
                                        selected = profile.profileId == selectedId,
                                        enabled = enabled,
                                        role = Role.RadioButton,
                                        onClick = { selectedId = profile.profileId },
                                    )
                                    .padding(vertical = 4.dp),
                            ) {
                                RadioButton(selected = profile.profileId == selectedId, onClick = null, enabled = enabled)
                                Column(Modifier.padding(start = 8.dp)) {
                                    Text(profile.name)
                                    if (!profile.hasPin) {
                                        Text(
                                            "No PIN set: sign in on a Sigil or add a PIN in the portal.",
                                            style = MaterialTheme.typography.bodySmall,
                                            color = MaterialTheme.colorScheme.onSurfaceVariant,
                                        )
                                    }
                                }
                            }
                        }
                    }
                }
                OutlinedTextField(
                    value = pin,
                    onValueChange = { value -> pin = value.filter(Char::isDigit).take(8) },
                    enabled = selected != null && !prompt.submitting,
                    singleLine = true,
                    label = { Text("PIN") },
                    visualTransformation = PasswordVisualTransformation(),
                    keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.NumberPassword, imeAction = ImeAction.Go),
                    keyboardActions = KeyboardActions(onGo = { submit() }),
                    modifier = Modifier.fillMaxWidth(),
                )
                prompt.error?.let {
                    Text(
                        it,
                        color = MaterialTheme.colorScheme.error,
                        modifier = Modifier.semantics { liveRegion = LiveRegionMode.Polite },
                    )
                }
            }
        },
        confirmButton = {
            TextButton(onClick = { submit() }, enabled = selected != null && pin.length >= 4 && !prompt.submitting) {
                Text(if (prompt.submitting) "Signing in…" else "Sign in")
            }
        },
        dismissButton = { TextButton(onClick = onDismiss) { Text("Cancel") } },
    )
}
