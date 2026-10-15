package com.turnhub.android.ui.tablet

import androidx.compose.foundation.layout.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.unit.dp
import com.turnhub.android.data.Personalization
import com.turnhub.android.data.ProfileSecret
import com.turnhub.android.standalone.*
import com.turnhub.android.ui.components.Tone
import com.turnhub.android.ui.components.ToneButton
import com.turnhub.android.ui.components.PlayerAvatar
import com.turnhub.android.ui.components.ColorPicker
import com.turnhub.android.ui.components.playerColor
import com.turnhub.android.ui.home.AvatarUploadControls
import com.turnhub.android.ui.theme.palette
import java.util.Base64
import kotlinx.coroutines.launch

@OptIn(ExperimentalLayoutApi::class)
@Composable
fun LocalAccountEditor(player: SavedLocalPlayer, table: StandaloneTable, destination: LocalImportState,
    canLink: Boolean, onLink: (String, String?, String) -> Unit, backgroundOnly: Boolean = false) {
    val scope = rememberCoroutineScope()
    var unlocked by remember(player.localId) { mutableStateOf(table.accountUnlocked(player.localId)) }
    var unlockSecret by remember(player.localId) { mutableStateOf("") }
    var credentialBusy by remember { mutableStateOf(false) }
    var credentialError by remember { mutableStateOf<String?>(null) }
    if (!unlocked) {
        OutlinedTextField(unlockSecret, { unlockSecret = it.take(ProfileSecret.MAX_CHARS) }, label = { Text("Account PIN or password") },
            visualTransformation = PasswordVisualTransformation(), singleLine = true, modifier = Modifier.fillMaxWidth())
        ToneButton("Unlock account settings", {
            scope.launch {
                credentialBusy = true
                unlocked = kotlinx.coroutines.withContext(kotlinx.coroutines.Dispatchers.Default) { table.unlockAccount(player.localId, unlockSecret) }
                unlockSecret = ""; credentialBusy = false
                credentialError = if (unlocked) null else "PIN or password was not accepted."
            }
        }, enabled = !credentialBusy && ProfileSecret.isValid(unlockSecret))
        credentialError?.let { Text(it, color = palette.warn) }
        return
    }
    if (backgroundOnly) {
        TileBackgroundEditor(player, table)
        return
    }
    var name by rememberSaveable(player.localId, player.name) { mutableStateOf(player.name) }
    var saveError by remember(player.localId) { mutableStateOf(false) }
    var styleError by remember(player.localId) { mutableStateOf(false) }
    val prefs = player.personalization
    // Avatar, motion and image changes say so when they don't save, like the name and color.
    fun saveStyle(next: LocalPersonalization) { styleError = !table.saveAccount(player.localId, player.name, next) }
    val icon = remember(prefs) { prefs.icon() }
    PlayerAvatar(player.name, 1, icon, size = 56.dp, color = playerColor(prefs.color, palette.avatarColor(1)))
    Text("Account ID ${player.profileId}", color = palette.muted)
    OutlinedTextField(name, { name = it.take(ProfileSecret.MAX_NAME_CHARS) }, label = { Text("Display name") },
        singleLine = true, modifier = Modifier.fillMaxWidth())
    ToneButton("Save name", { saveError = !table.saveAccount(player.localId, name, prefs) },
        enabled = name.isNotBlank() && name.trim() != player.name)
    Text("Player color", color = palette.text)
    ColorPicker(prefs.color, { chosen ->
        saveError = !table.saveAccount(player.localId, player.name, prefs.copy(color = chosen))
    }, palette.avatarColor(1))
    if (saveError) Text("Couldn’t save. Try again.", color = palette.warn)
    var newSecret by remember(player.localId) { mutableStateOf("") }
    OutlinedTextField(newSecret, { newSecret = it.take(ProfileSecret.MAX_CHARS) },
        label = { Text(if (player.credential == null) "Set a PIN or password" else "Change PIN or password") },
        visualTransformation = PasswordVisualTransformation(), singleLine = true, modifier = Modifier.fillMaxWidth())
    ToneButton("Save PIN or password", {
        scope.launch {
            credentialBusy = true
            val credential = kotlinx.coroutines.withContext(kotlinx.coroutines.Dispatchers.Default) { LocalCredential.create(newSecret) }
            val saved = table.setAccountCredential(player.localId, credential)
            newSecret = ""; credentialBusy = false
            credentialError = if (saved) "PIN or password saved." else "Could not save the PIN or password."
        }
    }, enabled = !credentialBusy && ProfileSecret.isValid(newSecret))
    if (player.credential != null) ToneButton("Remove PIN or password", {
        credentialError = if (table.clearAccountCredential(player.localId)) "PIN or password removed. Anyone can open these settings."
            else "Could not remove the PIN or password."
    }, enabled = !credentialBusy)
    credentialError?.let { Text(it, color = palette.muted) }
    Text("Avatar", color = palette.text)
    FlowRow(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
        LocalPersonalization.AVATARS.forEach { key ->
            FilterChip(prefs.avatar == key && prefs.image == null,
                { saveStyle(prefs.copy(avatar = key, image = null)) },
                label = { Text(key.replaceFirstChar { it.uppercase() }) })
        }
    }
    ToneButton("Reduced motion: ${if (prefs.reduceMotion) "On" else "Off"}", {
        saveStyle(prefs.copy(reduceMotion = !prefs.reduceMotion)) })
    AvatarUploadControls(Personalization(prefs.color, 0, true,
        customAvatar = if (prefs.image == null) "" else "local", customImage = icon?.image,
        uploadApprovalRequired = false), false,
        upload = { bytes, thumbnail -> saveStyle(prefs.copy(image = Base64.getEncoder().encodeToString(bytes), thumbnail = thumbnail)) },
        remove = { saveStyle(prefs.copy(image = null, thumbnail = null)) }, localOnly = true)
    if (styleError) Text("Couldn’t save. Try again.", color = palette.warn)
    TileBackgroundEditor(player, table)
    if (canLink && destination.atlasId != null) {
        val link = player.links.firstOrNull { it.atlasId == destination.atlasId }
        link?.let { Text("Linked to Atlas account ${it.profileId}. Matching games import automatically when signed in.", color = palette.muted) }
        var target by rememberSaveable(player.localId, destination.atlasId) { mutableStateOf("") }
        // Secrets deliberately do not survive saved instance state.
        var secret by remember(player.localId, destination.atlasId) { mutableStateOf("") }
        var expanded by remember { mutableStateOf(false) }
        Box {
            TextButton({ expanded = true }) { Text(destination.profiles.firstOrNull { it.profileId == target }
                ?.let { "Link to ${it.name} · ${it.profileId}" } ?: "Create this account on Atlas") }
            DropdownMenu(expanded, { expanded = false }) {
                DropdownMenuItem({ Text("Create this account on Atlas") }, { target = ""; expanded = false })
                destination.profiles.forEach { account ->
                    DropdownMenuItem({ Text("${account.name} · ${account.profileId}${if (account.hasPin) "" else " · set PIN"}") }, { target = account.profileId; expanded = false })
                }
            }
        }
        Text(if (target.isBlank()) if (player.credential == null) "Choose a PIN or password for the new Atlas account." else "Enter this local account's PIN or password to preserve it on Atlas."
            else "Enter that Atlas account's PIN or password to merge future imports. Both histories are retained; Atlas personalization stays unchanged.", color = palette.muted)
        OutlinedTextField(secret, { secret = it.take(ProfileSecret.MAX_CHARS) }, label = { Text("PIN or password") },
            visualTransformation = PasswordVisualTransformation(), singleLine = true, modifier = Modifier.fillMaxWidth())
        ToneButton(if (target.isBlank()) "Import account to Atlas" else "Verify and link account", {
            onLink(player.localId, target.ifBlank { null }, secret); secret = ""
        }, enabled = !destination.busy && ProfileSecret.isValid(secret))
        Text(ProfileSecret.RULE, color = palette.muted)
    }
    DeleteLocalAccount(player, table)
}

/** Deletes the account from this phone after a confirmation; its finished games stay in History. */
@Composable
private fun DeleteLocalAccount(player: SavedLocalPlayer, table: StandaloneTable) {
    var confirming by remember(player.localId) { mutableStateOf(false) }
    var refused by remember(player.localId) { mutableStateOf(false) }
    ToneButton("Delete account", { confirming = true }, Modifier.fillMaxWidth(), tone = Tone.BAD)
    if (refused) Text("Finish or end the current game first, then delete the account.", color = palette.warn)
    if (confirming) AlertDialog(
        onDismissRequest = { confirming = false },
        title = { Text("Delete ${player.name}?") },
        text = { Text("The account, its PIN and its look are removed from this phone. Finished games stay in History " +
            "under the name played with, but no longer count toward an account.") },
        confirmButton = { TextButton({ refused = !table.deleteAccount(player.localId); confirming = false }) { Text("Delete") } },
        dismissButton = { TextButton({ confirming = false }) { Text("Cancel") } },
    )
}
