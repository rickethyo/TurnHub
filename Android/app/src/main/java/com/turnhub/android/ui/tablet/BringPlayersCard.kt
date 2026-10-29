package com.turnhub.android.ui.tablet

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
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
    if (states.values.all { it == BringState.LINKED }) return
    BrassCard {
        Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
            Text("Bring players to this table", color = p.text, style = MaterialTheme.typography.titleMedium,
                modifier = Modifier.semantics { heading() })
            Text("Uploads your players to this Atlas with all their games.", color = p.muted)
            players.forEach { player ->
                when (states.getValue(player.localId)) {
                    BringState.LINKED -> Text("${player.name}: on this table", color = p.muted)
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
