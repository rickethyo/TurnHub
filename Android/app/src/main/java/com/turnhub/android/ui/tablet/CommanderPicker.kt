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
import com.turnhub.android.data.TileCardGame
import com.turnhub.android.domain.TablePlayer
import com.turnhub.android.standalone.StandaloneGame
import com.turnhub.android.ui.theme.palette
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch

/**
 * Optional: the card whose art fills a player's tile, in any game. A Magic card
 * can also be the commander a player brought, shown on their panel, the star and
 * in Commander damage ([commander] modes only). With card art on, matching names
 * are suggested as the player types, and each printing's art can be chosen. The
 * other games' cards ([TileCardGame]) and a random card from any of them are
 * tile art only, so they are never named as the commander.
 */
@OptIn(ExperimentalLayoutApi::class)
@Composable
internal fun CommanderPickerDialog(player: TablePlayer, commander: Boolean, gameCards: TileCardGame,
                                   onSave: (name: String?, printing: String?, cardOnly: Boolean) -> Unit, onDismiss: () -> Unit) {
    val p = palette
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    var drawing by remember { mutableStateOf(false) }
    var randomFailed by remember { mutableStateOf(false) }
    // Which game's cards the picker shows: the saved card's, else the game being played.
    var game by rememberSaveable { mutableStateOf(if (player.commander != null) TileCardGame.of(player.commanderArt) else gameCards) }
    // The card was drawn at random: tile art only, not this player's commander.
    var random by rememberSaveable { mutableStateOf(player.cardOnly && commander && player.commander != null) }
    val artOn = rememberCommanderArtEnabled().first
    var name by rememberSaveable { mutableStateOf(player.commander.orEmpty()) }
    // The card name the art list belongs to: a picked suggestion, or the saved card.
    var chosen by rememberSaveable { mutableStateOf(player.commander) }
    var printing by rememberSaveable { mutableStateOf(player.commanderArt) }
    var suggestions by remember { mutableStateOf(emptyList<String>()) }
    var printings by remember { mutableStateOf(emptyList<CommanderPrinting>()) }
    val magic = game == TileCardGame.MAGIC
    val asCommander = commander && magic && !random

    LaunchedEffect(name, artOn, game) {
        suggestions = emptyList()
        if (!artOn || name.trim().length < 2 || name == chosen) return@LaunchedEffect
        delay(300) // Wait for a pause in typing before asking.
        if (magic) suggestions = CommanderArtCache.source(context).suggest(name).take(6)
        else printings = CommanderArtCache.source(context).search(game, name)
    }
    LaunchedEffect(chosen, artOn, game) {
        val card = chosen ?: return@LaunchedEffect
        if (!artOn || !magic) return@LaunchedEffect
        printings = emptyList()
        printings = CommanderArtCache.source(context).printings(card)
    }

    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(if (commander) "${player.label}'s commander" else "${player.label}'s tile card") },
        text = {
            Column(Modifier.verticalScroll(rememberScrollState()), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                if (artOn) FlowRow(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                    TileCardGame.entries.forEach { option ->
                        FilterChip(game == option, {
                            if (game != option) {
                                game = option; name = ""; chosen = null; printing = null
                                printings = emptyList(); suggestions = emptyList(); random = false; randomFailed = false
                            }
                        }, label = { Text(option.label) })
                    }
                }
                OutlinedTextField(name, {
                    name = it.take(StandaloneGame.MAX_COMMANDER)
                    if (name != chosen) { chosen = null; printing = null; random = false; if (magic) printings = emptyList() }
                }, label = { Text(if (commander && magic) "Commander (optional)" else "Card (optional)") }, singleLine = true, modifier = Modifier.fillMaxWidth())
                if (artOn) {
                    OutlinedButton({
                        drawing = true; randomFailed = false
                        scope.launch {
                            val card = CommanderArtCache.source(context).random(game)
                            drawing = false
                            if (card == null) randomFailed = true
                            else {
                                name = card.name; chosen = card.name; printing = card.id; suggestions = emptyList(); random = true
                                if (!magic) printings = listOf(card)
                            }
                        }
                    }, enabled = !drawing, modifier = Modifier.fillMaxWidth()) {
                        Text(if (drawing) "Drawing a card…" else "Random ${game.label} card")
                    }
                    if (randomFailed) Text("Couldn't get a card. Try again when online.", color = p.bad, style = MaterialTheme.typography.bodySmall)
                    if (commander && name.isNotBlank() && !asCommander) Text(
                        if (magic) "A random card is only tile art; it isn't named as ${player.label}'s commander."
                        else "Only a Magic card can be a commander; this card is tile art only.",
                        color = p.muted, style = MaterialTheme.typography.bodySmall)
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
                if (printings.size > 1 || (!magic && printings.isNotEmpty())) {
                    Text(if (magic) "Card art for their tile" else "Pick a card for their tile", color = p.muted, style = MaterialTheme.typography.labelLarge)
                    LazyRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                        items(printings, key = { it.id }) { option ->
                            PrintingOption(option, showName = !magic, selected = (printing ?: printings.first().id.takeIf { magic }) == option.id) {
                                printing = option.id
                                if (!magic) { name = option.name; chosen = option.name }
                            }
                        }
                    }
                }
                Text(
                    when {
                        !artOn && commander -> "Card art is off in Settings, so names aren't looked up. Naming a commander is optional."
                        !artOn -> "Card art is off in Settings, so tiles show no card art."
                        magic && commander -> "Suggestions and card art come from Scryfall; only what you type is sent. Naming a commander is optional."
                        magic -> "The card's art fills this player's tile. Suggestions and art come from Scryfall; only what you type is sent."
                        else -> "The card's art fills this player's tile. Cards come from ${game.database}, an unofficial fan database; only what you type is sent."
                    },
                    color = p.faint, style = MaterialTheme.typography.bodySmall,
                )
            }
        },
        confirmButton = {
            // Another game's card needs a picked card: its name alone finds no art.
            val trimmed = name.trim().ifEmpty { null }
            TextButton({ onSave(trimmed, printing ?: printings.firstOrNull()?.id?.takeIf { magic }, !asCommander) },
                enabled = trimmed == null || magic || printing != null) { Text("Save") }
        },
        dismissButton = {
            if (player.commander != null) TextButton({ onSave(null, null, false) }) { Text("Clear") }
            else TextButton(onDismiss) { Text("Cancel") }
        },
    )
}

/** Where each game's cards come from, for the picker's note. */
private val TileCardGame.database: String get() = when (this) {
    TileCardGame.MAGIC -> "Scryfall"
    TileCardGame.POKEMON -> "TCGdex"
    TileCardGame.LORCANA -> "Lorcast"
    TileCardGame.YUGIOH -> "YGOPRODeck"
}

@Composable
private fun PrintingOption(option: CommanderPrinting, showName: Boolean, selected: Boolean, onPick: () -> Unit) {
    val p = palette
    val context = LocalContext.current
    val image by produceState<ImageBitmap?>(null, option.id) {
        val jpeg = CommanderArtCache.source(context).preview(option) ?: return@produceState
        value = BitmapFactory.decodeByteArray(jpeg, 0, jpeg.size)?.let { cropArt(it, TileCardGame.of(option.id).crop) }?.asImageBitmap()
    }
    Column(
        Modifier
            .width(132.dp)
            .clip(RoundedCornerShape(10.dp))
            .border(if (selected) 3.dp else 1.dp, if (selected) p.accent else p.line, RoundedCornerShape(10.dp))
            .clickable(onClickLabel = "Use this art", role = Role.Button, onClick = onPick)
            .semantics {
                this.selected = selected
                contentDescription = listOfNotNull(option.name.takeIf { showName }, option.setName.ifBlank { null },
                    option.artist.ifBlank { null }?.let { "art by $it" }).joinToString(", ")
            }
            .padding(6.dp),
        verticalArrangement = Arrangement.spacedBy(4.dp),
    ) {
        Box(Modifier.fillMaxWidth().height(90.dp).clip(RoundedCornerShape(6.dp))) {
            image?.let { Image(it, null, Modifier.fillMaxSize(), contentScale = ContentScale.Crop) }
        }
        if (showName) Text(option.name, color = p.text, style = MaterialTheme.typography.labelMedium, maxLines = 2, overflow = TextOverflow.Ellipsis)
        Text(option.setName, color = if (showName) p.muted else p.text, style = MaterialTheme.typography.labelMedium, maxLines = 1, overflow = TextOverflow.Ellipsis)
        Text(option.artist, color = p.muted, style = MaterialTheme.typography.labelSmall, maxLines = 1, overflow = TextOverflow.Ellipsis)
    }
}
