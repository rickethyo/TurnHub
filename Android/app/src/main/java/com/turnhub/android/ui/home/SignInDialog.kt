package com.turnhub.android.ui.home

import androidx.compose.animation.AnimatedVisibility
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.selection.selectable
import androidx.compose.foundation.selection.selectableGroup
import androidx.compose.foundation.selection.toggleable
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.ModalBottomSheet
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.rememberModalBottomSheetState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.semantics.LiveRegionMode
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.liveRegion
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.text.input.VisualTransformation
import androidx.compose.ui.unit.dp
import com.turnhub.android.data.ProfileSecret
import com.turnhub.android.protocol.ProfileSummary
import com.turnhub.android.ui.components.AccentButton
import com.turnhub.android.ui.components.GroupedList
import com.turnhub.android.ui.components.PlayerAvatar
import com.turnhub.android.ui.theme.DesignTokens
import com.turnhub.android.ui.theme.palette

/**
 * "Play from this phone": a sheet that rises from the bottom with Atlas's
 * accounts as a grouped list. Choosing one reveals its PIN or password field.
 * A profile without a secret yet (made on a Sigil or the tablet) asks for a
 * new one twice instead: Atlas keeps the first sign-in's PIN. Atlas checks the secret (and throttles guesses); the app
 * only checks ProfileSecret's rule. Where the phone supports it, "Sign in
 * automatically" keeps the secret behind the phone's own lock (ProfileVault).
 * "Create an account" switches the sheet to a name, secret and confirmation,
 * like the portal's Create account tab (`POST /api/profiles/register`).
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun SignInDialog(
    prompt: SignInPrompt,
    onSubmit: (profile: ProfileSummary, pin: String, remember: Boolean) -> Unit,
    onCreate: (name: String, pin: String, remember: Boolean) -> Unit,
    onDismiss: () -> Unit,
) {
    val p = palette
    var selectedId by rememberSaveable { mutableStateOf(prompt.preselect) }
    var pin by rememberSaveable { mutableStateOf("") }
    var shown by rememberSaveable { mutableStateOf(false) }
    var keep by rememberSaveable { mutableStateOf(true) }
    var creating by rememberSaveable { mutableStateOf(false) }
    var newName by rememberSaveable { mutableStateOf("") }
    var confirm by rememberSaveable { mutableStateOf("") }
    // With no accounts on this Atlas yet, the sheet opens straight on Create.
    val create = creating || (!prompt.loading && prompt.profiles.isEmpty() && prompt.error == null)
    val selected = prompt.profiles.firstOrNull { it.profileId == selectedId }
    // No PIN yet: this sign-in sets it, so it is typed twice like a new account's.
    val choosing = !create && selected?.hasPin == false
    val mismatch = confirm.isNotEmpty() && confirm != pin
    val canSubmit = !prompt.submitting && ProfileSecret.isValid(pin) &&
        if (create) newName.isNotBlank() && confirm == pin else selected != null && (!choosing || confirm == pin)
    val submit = {
        if (canSubmit) {
            val remember = prompt.offerRemember && keep
            if (create) onCreate(newName.trim(), pin, remember) else selected?.let { onSubmit(it, pin, remember) }
        }
    }
    val focus = remember { FocusRequester() }
    LaunchedEffect(selected?.profileId) { if (selected != null) runCatching { focus.requestFocus() } }

    ModalBottomSheet(
        onDismissRequest = onDismiss,
        sheetState = rememberModalBottomSheetState(skipPartiallyExpanded = true),
        containerColor = p.bg,
    ) {
        Column(
            Modifier
                .fillMaxWidth()
                .padding(horizontal = DesignTokens.Layout.gutter)
                .navigationBarsPadding()
                .padding(bottom = DesignTokens.Space.s5),
            verticalArrangement = Arrangement.spacedBy(DesignTokens.Space.s4),
        ) {
            Text(
                if (create) "Create an account" else "Play from this phone",
                style = MaterialTheme.typography.headlineSmall,
                color = p.text,
                modifier = Modifier.semantics { heading() },
            )
            Text(
                if (create) {
                    "Your name and statistics stay with this account. Use a PIN for quick sign-in on a Sigil, or a password."
                } else {
                    "Choose your account. Your name and statistics follow you to any table."
                },
                style = MaterialTheme.typography.bodyMedium,
                color = p.muted,
            )
            if (create) {
                OutlinedTextField(
                    value = newName,
                    onValueChange = { value -> newName = value.take(ProfileSecret.MAX_NAME_CHARS) },
                    enabled = !prompt.submitting,
                    singleLine = true,
                    label = { Text("Display name") },
                    keyboardOptions = KeyboardOptions(imeAction = ImeAction.Next),
                    modifier = Modifier.fillMaxWidth(),
                )
                OutlinedTextField(
                    value = pin,
                    onValueChange = { value -> pin = value.take(ProfileSecret.MAX_CHARS) },
                    enabled = !prompt.submitting,
                    singleLine = true,
                    label = { Text("PIN (4–8 digits) or password") },
                    visualTransformation = if (shown) VisualTransformation.None else PasswordVisualTransformation(),
                    trailingIcon = {
                        TextButton(onClick = { shown = !shown }) { Text(if (shown) "Hide" else "Show") }
                    },
                    keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Password, imeAction = ImeAction.Next),
                    modifier = Modifier.fillMaxWidth(),
                )
                OutlinedTextField(
                    value = confirm,
                    onValueChange = { value -> confirm = value.take(ProfileSecret.MAX_CHARS) },
                    enabled = !prompt.submitting,
                    singleLine = true,
                    isError = mismatch,
                    label = { Text("Type it again") },
                    visualTransformation = if (shown) VisualTransformation.None else PasswordVisualTransformation(),
                    keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Password, imeAction = ImeAction.Go),
                    keyboardActions = KeyboardActions(onGo = { submit() }),
                    modifier = Modifier.fillMaxWidth(),
                )
                if (mismatch) Text("The two entries don't match.", color = p.bad, style = MaterialTheme.typography.bodySmall)
            } else when {
                prompt.loading -> Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                    CircularProgressIndicator(Modifier.size(24.dp), strokeWidth = 2.dp)
                    Text("Loading accounts…", color = p.muted)
                }
                else -> GroupedList(Modifier.selectableGroup()) {
                    prompt.profiles.forEachIndexed { index, profile ->
                        val enabled = !prompt.submitting
                        val isSelected = profile.profileId == selectedId
                        row(
                            Modifier.selectable(
                                selected = isSelected,
                                enabled = enabled,
                                role = Role.RadioButton,
                                onClick = { selectedId = profile.profileId; pin = ""; confirm = "" },
                            ),
                        ) {
                            PlayerAvatar(profile.name, index + 1, null, size = 36.dp)
                            Column(Modifier.weight(1f)) {
                                Text(profile.name, color = if (enabled) p.text else p.faint, style = MaterialTheme.typography.bodyLarge)
                                if (!profile.hasPin) {
                                    Text(
                                        "No PIN or password yet: choose one to sign in.",
                                        style = MaterialTheme.typography.bodySmall,
                                        color = p.muted,
                                    )
                                }
                            }
                            if (isSelected) {
                                Icon(
                                    painterResource(DesignTokens.Icons.check),
                                    contentDescription = null,
                                    tint = p.accent,
                                    modifier = Modifier.size(22.dp),
                                )
                            }
                        }
                    }
                }
            }
            AnimatedVisibility(visible = !create && selected != null) {
                Column(verticalArrangement = Arrangement.spacedBy(DesignTokens.Space.s3)) {
                    OutlinedTextField(
                        value = pin,
                        onValueChange = { value -> pin = value.take(ProfileSecret.MAX_CHARS) },
                        enabled = !prompt.submitting,
                        singleLine = true,
                        label = { Text(if (choosing) "Choose a PIN (4–8 digits) or password" else "PIN or password") },
                        visualTransformation = if (shown) VisualTransformation.None else PasswordVisualTransformation(),
                        trailingIcon = {
                            TextButton(onClick = { shown = !shown }) { Text(if (shown) "Hide" else "Show") }
                        },
                        keyboardOptions = KeyboardOptions(
                            keyboardType = KeyboardType.Password,
                            imeAction = if (choosing) ImeAction.Next else ImeAction.Go,
                        ),
                        keyboardActions = KeyboardActions(onGo = { submit() }),
                        modifier = Modifier.fillMaxWidth().focusRequester(focus),
                    )
                    if (choosing) {
                        OutlinedTextField(
                            value = confirm,
                            onValueChange = { value -> confirm = value.take(ProfileSecret.MAX_CHARS) },
                            enabled = !prompt.submitting,
                            singleLine = true,
                            isError = mismatch,
                            label = { Text("Type it again") },
                            visualTransformation = if (shown) VisualTransformation.None else PasswordVisualTransformation(),
                            keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Password, imeAction = ImeAction.Go),
                            keyboardActions = KeyboardActions(onGo = { submit() }),
                            modifier = Modifier.fillMaxWidth(),
                        )
                        if (mismatch) Text("The two entries don't match.", color = p.bad, style = MaterialTheme.typography.bodySmall)
                    }
                }
            }
            if (prompt.offerRemember && (create || selected != null)) {
                GroupedList {
                    row(Modifier.toggleable(value = keep, role = Role.Switch, onValueChange = { keep = it })) {
                        Column(Modifier.weight(1f)) {
                            Text("Sign in automatically", color = p.text, style = MaterialTheme.typography.bodyLarge)
                            Text(
                                "On this phone, after your fingerprint, face or screen lock.",
                                color = p.muted,
                                style = MaterialTheme.typography.bodySmall,
                            )
                        }
                        Switch(checked = keep, onCheckedChange = null)
                    }
                }
            }
            prompt.error?.let {
                Text(
                    it,
                    color = p.bad,
                    style = MaterialTheme.typography.bodyMedium,
                    modifier = Modifier
                        .fillMaxWidth()
                        .background(p.bad.copy(alpha = .1f), MaterialTheme.shapes.medium)
                        .padding(12.dp)
                        .semantics { liveRegion = LiveRegionMode.Polite },
                )
            }
            AccentButton(
                when {
                    create && prompt.submitting -> "Creating account…"
                    create -> "Create account"
                    prompt.submitting -> "Signing in…"
                    else -> "Sign in"
                },
                { submit() },
                Modifier.fillMaxWidth(),
                enabled = canSubmit,
            )
            if (!prompt.loading && (creating || prompt.profiles.isNotEmpty())) {
                TextButton(
                    onClick = { creating = !creating; pin = ""; confirm = "" },
                    enabled = !prompt.submitting,
                    modifier = Modifier.fillMaxWidth(),
                ) { Text(if (creating) "I already have an account" else "New here? Create an account", color = p.accent) }
            }
            TextButton(onClick = onDismiss, modifier = Modifier.fillMaxWidth()) { Text("Cancel", color = p.muted) }
        }
    }
}
