package com.turnhub.android.ui.tablet

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.RadioButton
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.unit.dp
import com.turnhub.android.data.ProfileSecret
import com.turnhub.android.standalone.LocalImportState
import com.turnhub.android.standalone.SavedLocalPlayer
import com.turnhub.android.ui.components.BrassCard
import com.turnhub.android.ui.components.ToneButton
import com.turnhub.android.ui.theme.palette

/** Where one local account stands with the connected Atlas. */
enum class BringState { LINKED, ALREADY_THERE, NEW_NO_PIN, NEW_WITH_PIN }

fun bringState(player: SavedLocalPlayer, destination: LocalImportState): BringState = when {
    player.links.any { it.atlasId == destination.atlasId } -> BringState.LINKED
    destination.profiles.any { it.profileId == player.profileId } -> BringState.ALREADY_THERE
    player.credential == null -> BringState.NEW_NO_PIN
    else -> BringState.NEW_WITH_PIN
}

/**
 * "Bring players to this table" (plans: profile transfer): every local account
 * and where it stands with this Atlas, in words. PIN-less accounts go together
 * in one tap; an account with a PIN needs that PIN. Their games follow.
 */
@Composable
fun BringPlayersCard(players: List<SavedLocalPlayer>, destination: LocalImportState, enabled: Boolean,
                     onBring: (localId: String?, secret: String?) -> Unit) {
    val p = palette
    val states = players.associate { it.localId to bringState(it, destination) }
    val allHere = states.values.all { it == BringState.LINKED }
    BrassCard {
        Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
            Text(if (allHere) "Your players on this table" else "Bring players to this table", color = p.text,
                style = MaterialTheme.typography.titleMedium, modifier = Modifier.semantics { heading() })
            Text(if (allHere) "Their names and looks stay in sync with this Atlas; the newest change wins."
                else "Uploads your players to this Atlas with all their games.", color = p.muted)
            players.forEach { player ->
                when (states.getValue(player.localId)) {
                    BringState.LINKED -> Text("${player.name}: on this table" +
                        (destination.atlasTotals[player.localId]?.let { (played, won) -> " · $played played, $won won here" } ?: ""), color = p.muted)
                    BringState.ALREADY_THERE -> Text("${player.name}: this ID is already on this Atlas. Open Edit to link it with that account's PIN.", color = p.text)
                    BringState.NEW_NO_PIN -> Text("${player.name}: new here, no PIN. The first person to sign in sets one.", color = p.text)
                    BringState.NEW_WITH_PIN -> {
                        Text("${player.name}: new here, has a PIN", color = p.text)
                        // Secrets deliberately do not survive saved instance state.
                        var secret by remember(player.localId) { mutableStateOf("") }
                        OutlinedTextField(secret, { secret = it.take(ProfileSecret.MAX_CHARS) }, label = { Text("${player.name}'s PIN or password") },
                            visualTransformation = PasswordVisualTransformation(), singleLine = true, modifier = Modifier.fillMaxWidth())
                        ToneButton("Bring ${player.name}", { onBring(player.localId, secret); secret = "" },
                            enabled = enabled && ProfileSecret.isValid(secret))
                    }
                }
            }
            val pinless = states.values.count { it == BringState.NEW_NO_PIN }
            if (pinless > 0) ToneButton(if (pinless == 1) "Bring 1 player without a PIN" else "Bring $pinless players without a PIN",
                { onBring(null, null) }, Modifier.fillMaxWidth(), enabled = enabled)
        }
    }
}

/** Atlas accounts this device doesn't have yet: not here under their ID and not linked to a local account. */
fun restorableAccounts(players: List<SavedLocalPlayer>, destination: LocalImportState) = destination.profiles.filter { profile ->
    players.none { it.profileId == profile.profileId || it.links.any { l -> l.atlasId == destination.atlasId && l.profileId == profile.profileId } }
}

/**
 * "Get my account from this Atlas": after a fresh install or on a new device, an account
 * saved on this Atlas comes back under its own ID with its PIN, and its totals here.
 */
@Composable
fun RestoreAccountCard(players: List<SavedLocalPlayer>, destination: LocalImportState, enabled: Boolean,
                       onRestore: (profileId: String, secret: String) -> Unit) {
    val p = palette
    val accounts = restorableAccounts(players, destination)
    if (accounts.isEmpty()) return
    var open by remember { mutableStateOf(false) }
    var chosen by remember { mutableStateOf<String?>(null) }
    // Secrets deliberately do not survive saved instance state.
    var secret by remember { mutableStateOf("") }
    BrassCard {
        Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
            Text("Get my account from this Atlas", color = p.text,
                style = MaterialTheme.typography.titleMedium, modifier = Modifier.semantics { heading() })
            Text("New phone or reinstalled the app? Your account comes back with its PIN, look and totals on this Atlas.", color = p.muted)
            if (!open) ToneButton("Choose my account", { open = true }, Modifier.fillMaxWidth(), enabled = enabled)
            else {
                accounts.forEach { profile ->
                    Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
                        RadioButton(chosen == profile.profileId, { chosen = profile.profileId; secret = "" },
                            enabled = enabled && profile.hasPin)
                        Text(if (profile.hasPin) profile.name else "${profile.name} (no PIN yet: set one on Atlas first)",
                            color = if (profile.hasPin) p.text else p.muted)
                    }
                }
                val pick = accounts.firstOrNull { it.profileId == chosen }
                if (pick != null) {
                    OutlinedTextField(secret, { secret = it.take(ProfileSecret.MAX_CHARS) }, label = { Text("${pick.name}'s PIN or password") },
                        visualTransformation = PasswordVisualTransformation(), singleLine = true, modifier = Modifier.fillMaxWidth())
                    ToneButton("Get ${pick.name} on this device", { onRestore(pick.profileId, secret); secret = "" },
                        Modifier.fillMaxWidth(), enabled = enabled && ProfileSecret.isValid(secret))
                }
            }
        }
    }
}
