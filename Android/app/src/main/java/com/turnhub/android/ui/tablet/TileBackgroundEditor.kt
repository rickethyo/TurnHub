package com.turnhub.android.ui.tablet

import androidx.compose.foundation.layout.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.dp
import com.turnhub.android.standalone.LocalImportState
import com.turnhub.android.ui.components.BrassCard
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import com.turnhub.android.data.Personalization
import com.turnhub.android.domain.TileBackground
import com.turnhub.android.domain.TileBackgroundStyle
import com.turnhub.android.standalone.SavedLocalPlayer
import com.turnhub.android.standalone.StandaloneTable
import com.turnhub.android.ui.components.ToneButton
import com.turnhub.android.ui.home.AvatarUploadControls
import com.turnhub.android.ui.theme.palette
import java.util.Base64

@Composable
internal fun LocalTileBackgroundDialog(player: SavedLocalPlayer, table: StandaloneTable, onDismiss: () -> Unit) {
    androidx.compose.ui.window.Dialog(onDismissRequest = onDismiss) {
        BrassCard {
            Column(Modifier.heightIn(max = 560.dp).verticalScroll(rememberScrollState()),
                verticalArrangement = Arrangement.spacedBy(12.dp)) {
                Text("${player.name} · Tile background", color = palette.text, style = MaterialTheme.typography.titleMedium)
                LocalAccountEditor(player, table, LocalImportState(), false, { _, _, _ -> }, backgroundOnly = true)
                TextButton(onClick = onDismiss) { Text("Done") }
            }
        }
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
internal fun TileBackgroundEditor(player: SavedLocalPlayer, table: StandaloneTable) {
    val prefs = player.personalization
    val background = prefs.tileBackground
    var color by rememberSaveable(player.localId, background.color) { mutableStateOf(background.color.orEmpty()) }
    fun save(next: TileBackground) { table.saveAccount(player.localId, player.name, prefs.copy(tileBackground = next)) }
    Text("Tile background", color = palette.text, style = MaterialTheme.typography.titleMedium)
    Text("Follows this player across seats and games. Your avatar stays separate.", color = palette.muted,
        style = MaterialTheme.typography.bodySmall)
    FlowRow(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
        TileBackgroundStyle.entries.forEach { style ->
            FilterChip(selected = background.style == style, onClick = { save(background.copy(style = style)) },
                label = { Text(style.label) }, enabled = style != TileBackgroundStyle.IMAGE || background.image != null)
        }
    }
    OutlinedTextField(color, { color = it.take(7) }, label = { Text("Background color (#RRGGBB, or blank for player color)") },
        singleLine = true, modifier = Modifier.fillMaxWidth())
    ToneButton("Save background color", { save(background.copy(color = color.ifBlank { null })) },
        enabled = (color.isBlank() || color.matches(Regex("#[0-9a-fA-F]{6}"))) && color != background.color.orEmpty())
    val hue = prefs.color?.let { Color(android.graphics.Color.parseColor(it)) } ?: palette.avatarColor(1)
    Box(Modifier.fillMaxWidth().height(120.dp)) {
        PlayerTileBackground(background, hue, Modifier.fillMaxSize())
        Text(player.name, color = palette.text, modifier = Modifier.padding(12.dp))
    }
    AvatarUploadControls(current = Personalization(null, 0, true,
        customAvatar = if (background.image == null) "" else "local-background", uploadApprovalRequired = false),
        busy = false,
        upload = { bytes, _ -> save(background.copy(style = TileBackgroundStyle.IMAGE,
            image = Base64.getEncoder().encodeToString(bytes))) },
        remove = { save(background.copy(style = TileBackgroundStyle.DEFAULT, image = null)) },
        localOnly = true, imageKind = "tile background")
    ToneButton("Reset tile background", { save(TileBackground()) }, enabled = background != TileBackground())
}
