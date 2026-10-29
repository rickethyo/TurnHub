package com.turnhub.android.ui.tablet

import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
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
    DeviceAdminControls(player, table)
    var sharing by remember(player.localId) { mutableStateOf(false) }
    ToneButton("Use on another device", { sharing = true }, Modifier.fillMaxWidth())
    if (sharing) ShareAccountDialog(player) { sharing = false }
    DeleteLocalAccount(player, table)
}

/**
 * The device's Admin account (owner, 2026-10-29): the account type of whoever owns this phone or
 * tablet. It gives no extra powers here and stays on this device; on an Atlas it is a regular
 * account unless setup installs it as that Atlas's Admin.
 */
@Composable
private fun DeviceAdminControls(player: SavedLocalPlayer, table: StandaloneTable) {
    val library = table.state.collectAsState().value.library
    var refused by remember(player.localId) { mutableStateOf(false) }
    when {
        library.isDeviceAdmin(player.localId) ->
            Text("This device's Admin account. It keeps a PIN or password.", color = palette.muted)
        player.credential == null -> if (library.deviceAdmin == null)
            Text("Set a PIN or password to make this the device's Admin account.", color = palette.muted)
        else -> {
            val current = library.players.firstOrNull { library.isDeviceAdmin(it.localId) }
            ToneButton("Make this the device Admin", { refused = !table.setDeviceAdmin(player.localId) }, Modifier.fillMaxWidth())
            if (current != null) Text("${current.name} is the Admin now. Unlock ${current.name}'s settings first to hand it over.",
                color = palette.muted)
        }
    }
    if (refused) Text("Couldn't change the device Admin.", color = palette.warn)
}

/**
 * First-time setup on a phone or tablet: the owner's account becomes the device's Admin account,
 * either a new one or one already here (devices set up before Admin accounts existed).
 * "Not now" closes it; an account can still be made Admin from its settings.
 */
@Composable
fun DeviceAdminSetupDialog(library: LocalLibrary, table: StandaloneTable) {
    val p = palette
    val scope = rememberCoroutineScope()
    var name by rememberSaveable { mutableStateOf("") }
    // An account already on this device to promote; null makes a new one.
    var chosenId by rememberSaveable { mutableStateOf<String?>(null) }
    val chosen = library.players.firstOrNull { it.localId == chosenId }
    // Secrets deliberately do not survive saved instance state.
    var secret by remember { mutableStateOf("") }
    var confirm by remember { mutableStateOf("") }
    var saving by remember { mutableStateOf(false) }
    var error by remember { mutableStateOf<String?>(null) }
    var copying by remember { mutableStateOf(false) }
    if (copying) {
        CopyAccountDialog(table, admin = true) { copying = false }
        return
    }
    fun choose(localId: String?) { chosenId = localId; secret = ""; confirm = ""; error = null }
    val unlocking = chosen?.credential != null
    val ready = when {
        chosen == null -> name.isNotBlank() && ProfileSecret.isValid(secret) && confirm == secret
        unlocking -> secret.isNotEmpty()
        else -> ProfileSecret.isValid(secret) && confirm == secret
    }
    AlertDialog(
        onDismissRequest = {},
        containerColor = p.surface,
        title = { Text("Set up your Admin account") },
        text = {
            Column(Modifier.verticalScroll(rememberScrollState()), verticalArrangement = Arrangement.spacedBy(12.dp)) {
                Text("Every TurnHub device has one Admin account: yours, as its owner. Every player at the table " +
                    "stays equal. You can add a separate everyday profile later.", color = p.muted)
                if (library.players.isNotEmpty()) {
                    Text("Is your account already here? Choose it:", color = p.muted)
                    library.players.forEach { player ->
                        Row(Modifier.fillMaxWidth(), verticalAlignment = androidx.compose.ui.Alignment.CenterVertically) {
                            RadioButton(chosenId == player.localId, { choose(player.localId) }, enabled = !saving)
                            Text(player.name)
                        }
                    }
                    Row(Modifier.fillMaxWidth(), verticalAlignment = androidx.compose.ui.Alignment.CenterVertically) {
                        RadioButton(chosenId == null, { choose(null) }, enabled = !saving)
                        Text("A new account")
                    }
                }
                if (chosen == null) OutlinedTextField(name, { name = it.take(ProfileSecret.MAX_NAME_CHARS) },
                    label = { Text("Your name") }, enabled = !saving, singleLine = true, modifier = Modifier.fillMaxWidth())
                OutlinedTextField(secret, { secret = it.take(ProfileSecret.MAX_CHARS); error = null },
                    label = { Text(when {
                        unlocking -> "${chosen!!.name}'s PIN or password"
                        chosen != null -> "Choose a PIN or password for ${chosen.name}"
                        else -> "PIN (4–8 digits) or password"
                    }) },
                    enabled = !saving, singleLine = true, visualTransformation = PasswordVisualTransformation(),
                    modifier = Modifier.fillMaxWidth())
                if (!unlocking) {
                    OutlinedTextField(confirm, { confirm = it.take(ProfileSecret.MAX_CHARS) }, label = { Text("Type it again") },
                        enabled = !saving, singleLine = true, visualTransformation = PasswordVisualTransformation(),
                        isError = confirm.isNotEmpty() && confirm != secret, modifier = Modifier.fillMaxWidth())
                    Text(if (chosen != null) "The Admin account always keeps a PIN or password. ${ProfileSecret.RULE}"
                        else ProfileSecret.RULE, color = p.muted)
                }
                error?.let { Text(it, color = p.warn) }
                TextButton({ copying = true }, enabled = !saving) { Text("Use my account from another device") }
            }
        },
        confirmButton = {
            TextButton(enabled = !saving && ready, onClick = {
                saving = true; error = null
                scope.launch {
                    val done = kotlinx.coroutines.withContext(kotlinx.coroutines.Dispatchers.Default) {
                        when {
                            chosen == null -> table.createAdminAccount(name, LocalCredential.create(secret))
                            unlocking -> table.promoteToDeviceAdmin(chosen.localId, secret)
                            else -> table.promoteToDeviceAdmin(chosen.localId, null, LocalCredential.create(secret))
                        }
                    }
                    if (!done) error = if (unlocking) "That PIN or password doesn't match ${chosen!!.name}'s."
                        else "Could not save your account. Please try again."
                    saving = false
                }
            }) { Text(when {
                saving -> "Saving…"
                chosen != null -> "Make ${chosen.name} the Admin"
                else -> "Create Admin account"
            }) }
        },
        dismissButton = { TextButton({ table.dismissAdminPrompt() }, enabled = !saving) { Text("Not now") } },
    )
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
