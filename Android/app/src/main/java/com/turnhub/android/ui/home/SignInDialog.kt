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
 * Profiles without a secret can only sign in physically, so they are listed
 * but not selectable. Atlas checks the secret (and throttles guesses); the app
 * only checks ProfileSecret's rule. Where the phone supports it, "Sign in
 * automatically" keeps the secret behind the phone's own lock (ProfileVault).
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun SignInDialog(
    prompt: SignInPrompt,
    onSubmit: (profile: ProfileSummary, pin: String, remember: Boolean) -> Unit,
    onDismiss: () -> Unit,
) {
    val p = palette
    var selectedId by rememberSaveable { mutableStateOf(prompt.preselect) }
    var pin by rememberSaveable { mutableStateOf("") }
    var shown by rememberSaveable { mutableStateOf(false) }
    var keep by rememberSaveable { mutableStateOf(true) }
    val selected = prompt.profiles.firstOrNull { it.profileId == selectedId && it.hasPin }
    val canSubmit = selected != null && ProfileSecret.isValid(pin) && !prompt.submitting
    val submit = { if (canSubmit) selected?.let { onSubmit(it, pin, prompt.offerRemember && keep) } }
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
                "Play from this phone",
                style = MaterialTheme.typography.headlineSmall,
                color = p.text,
                modifier = Modifier.semantics { heading() },
            )
            Text(
                "Choose your account. Your name and statistics follow you to any table.",
                style = MaterialTheme.typography.bodyMedium,
                color = p.muted,
            )
            when {
                prompt.loading -> Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                    CircularProgressIndicator(Modifier.size(24.dp), strokeWidth = 2.dp)
                    Text("Loading accounts…", color = p.muted)
                }
                prompt.profiles.isEmpty() && prompt.error == null ->
                    Text("No accounts yet. Create one in the Atlas portal first.", color = p.muted)
                else -> GroupedList(Modifier.selectableGroup()) {
                    prompt.profiles.forEachIndexed { index, profile ->
                        val enabled = profile.hasPin && !prompt.submitting
                        val isSelected = profile.profileId == selectedId
                        row(
                            Modifier.selectable(
                                selected = isSelected,
                                enabled = enabled,
                                role = Role.RadioButton,
                                onClick = { selectedId = profile.profileId; pin = "" },
                            ),
                        ) {
                            PlayerAvatar(profile.name, index + 1, null, size = 36.dp)
                            Column(Modifier.weight(1f)) {
                                Text(profile.name, color = if (enabled) p.text else p.faint, style = MaterialTheme.typography.bodyLarge)
                                if (!profile.hasPin) {
                                    Text(
                                        "No PIN or password yet: sign in on a Sigil or in the portal first.",
                                        style = MaterialTheme.typography.bodySmall,
                                        color = p.faint,
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
            AnimatedVisibility(visible = selected != null) {
                Column(verticalArrangement = Arrangement.spacedBy(DesignTokens.Space.s4)) {
                    OutlinedTextField(
                        value = pin,
                        onValueChange = { value -> pin = value.take(ProfileSecret.MAX_CHARS) },
                        enabled = !prompt.submitting,
                        singleLine = true,
                        label = { Text("PIN or password") },
                        visualTransformation = if (shown) VisualTransformation.None else PasswordVisualTransformation(),
                        trailingIcon = {
                            TextButton(onClick = { shown = !shown }) { Text(if (shown) "Hide" else "Show") }
                        },
                        keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Password, imeAction = ImeAction.Go),
                        keyboardActions = KeyboardActions(onGo = { submit() }),
                        modifier = Modifier.fillMaxWidth().focusRequester(focus),
                    )
                    if (prompt.offerRemember) {
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
                if (prompt.submitting) "Signing in…" else "Sign in",
                { submit() },
                Modifier.fillMaxWidth(),
                enabled = canSubmit,
            )
            TextButton(onClick = onDismiss, modifier = Modifier.fillMaxWidth()) { Text("Cancel", color = p.muted) }
        }
    }
}
