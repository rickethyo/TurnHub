package com.turnhub.android.ui.home

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.selection.selectable
import androidx.compose.foundation.selection.toggleable
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.RadioButton
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.LiveRegionMode
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.liveRegion
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.unit.dp
import com.turnhub.android.data.PlayerSessionState
import com.turnhub.android.ui.components.AccentButton
import com.turnhub.android.ui.components.AvatarGlyph
import com.turnhub.android.ui.components.BrassCard
import com.turnhub.android.ui.components.Eyebrow
import com.turnhub.android.ui.components.PlayerAvatar
import com.turnhub.android.ui.components.StatusRow
import com.turnhub.android.ui.components.Tone
import com.turnhub.android.ui.components.ToneButton
import com.turnhub.android.ui.theme.TurnHubPalette
import com.turnhub.android.ui.theme.TurnHubThemeChoice
import com.turnhub.android.ui.theme.palette

data class AccountActions(
    val onPlayFromPhone: () -> Unit = {},
    val onSaveName: (String) -> Unit = {},
    val onSavePin: (String) -> Unit = {},
    val onLoadPersonalization: () -> Unit = {},
    val onSavePersonalization: (color: String?, avatar: Int?) -> Unit = { _, _ -> },
    val onAccessibility: () -> Unit = {},
    val onSignOut: () -> Unit = {},
    val onDisconnect: () -> Unit = {},
    val onThemeChosen: (TurnHubThemeChoice) -> Unit = {},
    val onReduceMotion: (Boolean) -> Unit = {},
)

/** Sigil light colors to pick from (the portal offers a free color picker; these cover the wheel). */
private val JEWEL_COLORS = listOf(
    "#ff3b30", "#ff8800", "#ffd400", "#7ed957", "#00c8c8", "#1e90ff", "#6a5cff", "#c04dff", "#ff4fa3", "#ffffff",
)

@Composable
fun AccountTab(
    uiState: HomeUiState,
    theme: TurnHubThemeChoice,
    reduceMotion: Boolean,
    actions: AccountActions,
) {
    val p = palette
    val session = uiState.player?.session as? PlayerSessionState.SignedIn
    Column(verticalArrangement = Arrangement.spacedBy(14.dp)) {
        if (session == null) {
            BrassCard {
                Eyebrow("My account")
                Text(
                    "Sign in to save your name, PIN, avatar, Sigil light color and accessibility choices.",
                    color = p.muted,
                )
                AccentButton("Sign in", actions.onPlayFromPhone, Modifier.fillMaxWidth(), enabled = uiState.tableSummary != null)
            }
        } else {
            ProfileCard(uiState, session, actions)
            PersonalizationCard(uiState, actions)
            BrassCard {
                Eyebrow("Sigil accessibility")
                Text(
                    "Saved with your profile on Atlas and follows you to whichever Sigil you sit at: buzzer sound, " +
                        "light style and how long buttons must be held.",
                    color = p.muted,
                    style = MaterialTheme.typography.bodySmall,
                )
                ToneButton("Sigil accessibility…", actions.onAccessibility, Modifier.fillMaxWidth(), tone = Tone.INFO)
            }
        }
        AppearanceCard(theme, reduceMotion, actions)
        BrassCard {
            Eyebrow("Atlas connection")
            uiState.tableSummary?.let {
                StatusRow("Atlas", it.atlasId)
                StatusRow("Firmware", it.firmwareVersion)
            }
            StatusRow("Address", uiState.endpointText)
            StatusRow("Connection", uiState.connectionState.name.lowercase().replaceFirstChar { it.uppercase() })
            ToneButton("Disconnect", actions.onDisconnect, Modifier.fillMaxWidth(), tone = Tone.WARN)
        }
    }
}

@Composable
private fun ProfileCard(uiState: HomeUiState, session: PlayerSessionState.SignedIn, actions: AccountActions) {
    val p = palette
    var name by rememberSaveable(session.profileId) { mutableStateOf(session.name.orEmpty()) }
    var pin by rememberSaveable { mutableStateOf("") }
    val me = uiState.me()
    BrassCard {
        Eyebrow("My account")
        Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(14.dp)) {
            val icon = uiState.avatars.firstOrNull { it.id == uiState.personalization?.avatar } ?: me?.avatar
            PlayerAvatar(session.name ?: "?", me?.playerNumber ?: 0, icon, size = 56.dp)
            Column(Modifier.weight(1f)) {
                Text(session.name ?: "Unnamed profile", color = p.text, style = MaterialTheme.typography.titleLarge)
                Text("Profile ${session.profileId}", color = p.faint, style = MaterialTheme.typography.bodySmall)
            }
        }
        uiState.player?.feedback?.let {
            Text(it.message, color = if (it.isError) p.bad else p.good, style = MaterialTheme.typography.bodyMedium)
        }
        Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            OutlinedTextField(
                value = name,
                onValueChange = { name = it.take(32) },
                label = { Text("Display name") },
                singleLine = true,
                modifier = Modifier.weight(1f),
            )
            ToneButton("Save", { actions.onSaveName(name) }, enabled = name.isNotBlank() && name != session.name)
        }
        Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            OutlinedTextField(
                value = pin,
                onValueChange = { pin = it.filter(Char::isDigit).take(8) },
                label = { Text("New PIN (4–8 digits)") },
                singleLine = true,
                visualTransformation = PasswordVisualTransformation(),
                keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.NumberPassword),
                modifier = Modifier.weight(1f),
            )
            ToneButton("Set PIN", { actions.onSavePin(pin); pin = "" }, enabled = pin.length in 4..8)
        }
        ToneButton("Log out", actions.onSignOut, Modifier.fillMaxWidth(), tone = Tone.BAD)
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun PersonalizationCard(uiState: HomeUiState, actions: AccountActions) {
    val p = palette
    LaunchedEffect(Unit) { actions.onLoadPersonalization() }
    val current = uiState.personalization
    // Chosen here, sent only by Save; a fresh load from Atlas resets them.
    var avatarDraft by remember(current) { mutableStateOf(current?.avatar ?: 0) }
    var colorDraft by remember(current) { mutableStateOf(current?.color) }  // null = standard colors.
    val dirty = current != null && (avatarDraft != current.avatar || !colorDraft.equals(current.color, ignoreCase = true))
    BrassCard {
        Eyebrow("Personalization")
        Text("How you show up at the table. Saved with your profile on Atlas's microSD card.", color = p.muted, style = MaterialTheme.typography.bodySmall)
        if (current != null && !current.cardPresent) {
            Text("Insert a microSD card in Atlas to choose an avatar and color.", color = p.warn)
        }
        Text("Avatar", color = p.text, style = MaterialTheme.typography.titleSmall)
        FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
            AvatarChoice(label = "None", selected = avatarDraft == 0, onClick = { avatarDraft = 0 }) {
                Text("–", color = p.avatarText)
            }
            uiState.avatars.forEach { icon ->
                AvatarChoice(label = icon.label, selected = avatarDraft == icon.id, onClick = { avatarDraft = icon.id }) {
                    AvatarGlyph(icon, p.avatarText, 30.dp)
                }
            }
        }
        Text("Sigil light color", color = p.text, style = MaterialTheme.typography.titleSmall)
        FlowRow(horizontalArrangement = Arrangement.spacedBy(10.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
            JEWEL_COLORS.forEach { hex ->
                val selected = colorDraft.equals(hex, ignoreCase = true)
                Box(
                    Modifier
                        .size(40.dp)
                        .clip(CircleShape)
                        .background(Color(android.graphics.Color.parseColor(hex)))
                        .border(if (selected) 3.dp else 1.dp, if (selected) p.accentHi else p.lineStrong, CircleShape)
                        .selectable(selected = selected, role = Role.RadioButton) { colorDraft = hex }
                        .semantics { contentDescription = "Color $hex" },
                    contentAlignment = Alignment.Center,
                ) {
                    if (selected) Text("✓", color = Color.Black)
                }
            }
        }
        ToneButton("Standard colors", { colorDraft = null }, enabled = colorDraft != null)
        Text(
            "Your Sigil's ring glows in this color while you wait in the lobby and between turns. Turns, pauses, " +
                "warnings and wins keep their usual colors.",
            color = p.faint,
            style = MaterialTheme.typography.bodySmall,
        )
        AccentButton(
            if (dirty) "Save personalization" else "Personalization saved",
            { actions.onSavePersonalization(colorDraft ?: "none", avatarDraft) },
            Modifier.fillMaxWidth(),
            enabled = dirty && current?.cardPresent == true,
        )
    }
}

@Composable
private fun AvatarChoice(label: String, selected: Boolean, onClick: () -> Unit, content: @Composable () -> Unit) {
    val p = palette
    Column(
        Modifier
            .width(76.dp)
            .clip(RoundedCornerShape(12.dp))
            .background(p.inset)
            .border(if (selected) 2.dp else 1.dp, if (selected) p.accent else p.line, RoundedCornerShape(12.dp))
            .selectable(selected = selected, role = Role.RadioButton, onClick = onClick)
            .padding(8.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        Box(Modifier.size(44.dp).clip(CircleShape).background(p.avatarColor(1)), contentAlignment = Alignment.Center) { content() }
        Text(label, color = p.muted, style = MaterialTheme.typography.labelSmall, maxLines = 1)
    }
}

@Composable
private fun AppearanceCard(theme: TurnHubThemeChoice, reduceMotion: Boolean, actions: AccountActions) {
    val p = palette
    // Chosen here, applied and stored only by Save.
    var themeDraft by remember(theme) { mutableStateOf(theme) }
    var motionDraft by remember(reduceMotion) { mutableStateOf(reduceMotion) }
    var savedNote by remember { mutableStateOf(false) }
    val dirty = themeDraft != theme || motionDraft != reduceMotion
    BrassCard {
        Eyebrow("Appearance")
        Text("Themes change only how this phone looks. They never change the game.", color = p.muted, style = MaterialTheme.typography.bodySmall)
        TurnHubThemeChoice.entries.forEach { choice ->
            val swatch = TurnHubPalette.of(choice)
            Row(
                Modifier
                    .fillMaxWidth()
                    .clip(RoundedCornerShape(14.dp))
                    .background(p.inset)
                    .border(if (choice == themeDraft) 2.dp else 1.dp, if (choice == themeDraft) p.accent else p.line, RoundedCornerShape(14.dp))
                    .selectable(selected = choice == themeDraft, role = Role.RadioButton) { themeDraft = choice; savedNote = false }
                    .padding(12.dp),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(12.dp),
            ) {
                RadioButton(selected = choice == themeDraft, onClick = null)
                Column(Modifier.weight(1f)) {
                    Text(choice.label, color = p.text, style = MaterialTheme.typography.titleSmall)
                    Text(choice.blurb, color = p.muted, style = MaterialTheme.typography.bodySmall)
                }
                Row(Modifier.clip(RoundedCornerShape(6.dp)).border(1.dp, p.lineStrong, RoundedCornerShape(6.dp))) {
                    listOf(swatch.bg, swatch.surface3, swatch.accent, swatch.active).forEach {
                        Box(Modifier.size(width = 12.dp, height = 24.dp).background(it))
                    }
                }
            }
        }
        Row(
            Modifier
                .fillMaxWidth()
                .toggleable(value = motionDraft, role = Role.Switch, onValueChange = { motionDraft = it; savedNote = false })
                .padding(vertical = 6.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Column(Modifier.weight(1f)) {
                Text("Reduce motion", color = p.text, style = MaterialTheme.typography.titleSmall)
                Text("Stops the turning gear and gauge sweeps in this app.", color = p.muted, style = MaterialTheme.typography.bodySmall)
            }
            Switch(checked = motionDraft, onCheckedChange = null)
        }
        AccentButton(
            if (dirty) "Save appearance" else "Appearance saved",
            {
                actions.onThemeChosen(themeDraft)
                actions.onReduceMotion(motionDraft)
                savedNote = true
            },
            Modifier.fillMaxWidth(),
            enabled = dirty,
        )
        if (savedNote && !dirty) {
            Text(
                "Saved on this phone.",
                color = p.good,
                style = MaterialTheme.typography.bodySmall,
                modifier = Modifier.semantics { liveRegion = LiveRegionMode.Polite },
            )
        }
    }
}
