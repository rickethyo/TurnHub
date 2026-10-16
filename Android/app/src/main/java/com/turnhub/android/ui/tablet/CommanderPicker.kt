package com.turnhub.android.ui.tablet

import android.graphics.BitmapFactory
import androidx.compose.foundation.Image
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import com.turnhub.android.data.CommanderPrinting
import com.turnhub.android.domain.TablePlayer
import com.turnhub.android.standalone.StandaloneGame
import com.turnhub.android.ui.theme.palette
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch

/**
 * Optional: the commander a player brought, shown on their panel, the star and
 * in Commander damage. With card art on, matching card names are suggested as
 * the player types, and each printing's art can be chosen for their tile. In
 * the other Magic formats ([commander] false) it is only a card for the tile,
 * and nothing calls it a commander. A random card can fill it in any Magic
 * format; it is only tile art, so it is never named as the commander.
 */
@Composable
internal fun CommanderPickerDialog(player: TablePlayer, commander: Boolean,
                                   onSave: (name: String?, printing: String?, cardOnly: Boolean) -> Unit, onDismiss: () -> Unit) {
    val p = palette
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    var drawing by remember { mutableStateOf(false) }
    var randomFailed by remember { mutableStateOf(false) }
    // The card was drawn at random: tile art only, not this player's commander.
    var random by rememberSaveable { mutableStateOf(player.cardOnly && commander && player.commander != null) }
    val artOn = rememberCommanderArtEnabled().first
    var name by rememberSaveable { mutableStateOf(player.commander.orEmpty()) }
    // The card name the art list belongs to: a picked suggestion, or the saved commander.
    var chosen by rememberSaveable { mutableStateOf(player.commander) }
    var printing by rememberSaveable { mutableStateOf(player.commanderArt) }
    var suggestions by remember { mutableStateOf(emptyList<String>()) }
    var printings by remember { mutableStateOf(emptyList<CommanderPrinting>()) }

    LaunchedEffect(name, artOn) {
        suggestions = emptyList()
        if (!artOn || name.trim().length < 2 || name == chosen) return@LaunchedEffect
        delay(300) // Wait for a pause in typing before asking Scryfall.
        suggestions = CommanderArtCache.source(context).suggest(name).take(6)
    }
    LaunchedEffect(chosen, artOn) {
        printings = emptyList()
        val card = chosen ?: return@LaunchedEffect
        if (artOn) printings = CommanderArtCache.source(context).printings(card)
    }

    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(if (commander) "${player.label}'s commander" else "${player.label}'s tile card") },
        text = {
            Column(Modifier.verticalScroll(rememberScrollState()), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                OutlinedTextField(name, {
                    name = it.take(StandaloneGame.MAX_COMMANDER)
                    if (name != chosen) { chosen = null; printing = null; random = false }
                }, label = { Text(if (commander) "Commander (optional)" else "Card (optional)") }, singleLine = true, modifier = Modifier.fillMaxWidth())
                if (artOn) {
                    OutlinedButton({
                        drawing = true; randomFailed = false
                        scope.launch {
                            val card = CommanderArtCache.source(context).random()
                            drawing = false
                            if (card == null) randomFailed = true
                            else { name = card.first; chosen = card.first; printing = card.second; suggestions = emptyList(); random = true }
                        }
                    }, enabled = !drawing, modifier = Modifier.fillMaxWidth()) {
                        Text(if (drawing) "Drawing a card…" else "Random card")
                    }
                    if (random && commander) Text("A random card is only tile art; it isn't named as ${player.label}'s commander.",
                        color = p.muted, style = MaterialTheme.typography.bodySmall)
                    if (randomFailed) Text("Couldn't reach Scryfall. Try again when online.", color = p.bad, style = MaterialTheme.typography.bodySmall)
                }
                suggestions.forEach { suggestion ->
                    Text(suggestion, color = p.text, style = MaterialTheme.typography.bodyLarge,
                        maxLines = 1, overflow = TextOverflow.Ellipsis,
                        modifier = Modifier
                            .fillMaxWidth()
                            .clip(RoundedCornerShape(8.dp))
                            .clickable(onClickLabel = "Use $suggestion", role = Role.Button) {
                                name = suggestion; chosen = suggestion; printing = null; suggestions = emptyList(); random = false
                            }
                            .padding(horizontal = 8.dp, vertical = 10.dp))
                }
                if (printings.size > 1) {
                    Text("Card art for their tile", color = p.muted, style = MaterialTheme.typography.labelLarge)
                    LazyRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                        items(printings, key = { it.id }) { option ->
                            PrintingOption(option, selected = (printing ?: printings.first().id) == option.id) { printing = option.id }
                        }
                    }
                }
                Text(
                    when {
                        artOn && commander -> "Suggestions and card art come from Scryfall; only what you type is sent. Naming a commander is optional."
                        artOn -> "The card's art fills this player's tile. Suggestions and art come from Scryfall; only what you type is sent."
                        commander -> "Card art is off in Settings, so names aren't looked up. Naming a commander is optional."
                        else -> "Card art is off in Settings, so tiles show no card art."
                    },
                    color = p.faint, style = MaterialTheme.typography.bodySmall,
                )
            }
        },
        confirmButton = { TextButton({ onSave(name.trim().ifEmpty { null }, printing ?: printings.firstOrNull()?.id, random || !commander) }) { Text("Save") } },
        dismissButton = {
            if (player.commander != null) TextButton({ onSave(null, null, false) }) { Text("Clear") }
            else TextButton(onDismiss) { Text("Cancel") }
        },
    )
}

@Composable
private fun PrintingOption(option: CommanderPrinting, selected: Boolean, onPick: () -> Unit) {
    val p = palette
    val context = LocalContext.current
    val image by produceState<ImageBitmap?>(null, option.id) {
        val jpeg = CommanderArtCache.source(context).preview(option) ?: return@produceState
        value = BitmapFactory.decodeByteArray(jpeg, 0, jpeg.size)?.asImageBitmap()
    }
    Column(
        Modifier
            .width(132.dp)
            .clip(RoundedCornerShape(10.dp))
            .border(if (selected) 3.dp else 1.dp, if (selected) p.accent else p.line, RoundedCornerShape(10.dp))
            .clickable(onClickLabel = "Use this art", role = Role.Button, onClick = onPick)
            .semantics {
                this.selected = selected
                contentDescription = "${option.setName} art by ${option.artist}"
            }
            .padding(6.dp),
        verticalArrangement = Arrangement.spacedBy(4.dp),
    ) {
        Box(Modifier.fillMaxWidth().height(90.dp).clip(RoundedCornerShape(6.dp))) {
            image?.let { Image(it, null, Modifier.fillMaxSize(), contentScale = ContentScale.Crop) }
        }
        Text(option.setName, color = p.text, style = MaterialTheme.typography.labelMedium, maxLines = 1, overflow = TextOverflow.Ellipsis)
        Text(option.artist, color = p.muted, style = MaterialTheme.typography.labelSmall, maxLines = 1, overflow = TextOverflow.Ellipsis)
    }
}
