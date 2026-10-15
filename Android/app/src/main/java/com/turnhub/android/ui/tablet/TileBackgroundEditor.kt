package com.turnhub.android.ui.tablet

import androidx.compose.foundation.layout.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
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
import com.turnhub.android.ui.components.ColorPicker
import com.turnhub.android.ui.components.playerColor
import com.turnhub.android.ui.home.AvatarUploadControls
import com.turnhub.android.ui.theme.palette
import java.util.Base64

@Composable
internal fun LocalTileBackgroundDialog(player: SavedLocalPlayer, table: StandaloneTable, onDismiss: () -> Unit) {
    androidx.compose.ui.window.Dialog(onDismissRequest = onDismiss) {
        BrassCard(Modifier.heightIn(max = 600.dp)) {
            Text("${player.name} · Tile background", color = palette.text, style = MaterialTheme.typography.titleMedium)
            Column(Modifier.weight(1f, fill = false).verticalScroll(rememberScrollState()),
                verticalArrangement = Arrangement.spacedBy(12.dp)) {
                LocalAccountEditor(player, table, LocalImportState(), false, { _, _, _ -> }, backgroundOnly = true)
            }
            TextButton(onClick = onDismiss) { Text("Done") }
        }
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
internal fun TileBackgroundEditor(player: SavedLocalPlayer, table: StandaloneTable) {
    val prefs = player.personalization
    val background = prefs.tileBackground
    var saveError by remember(player.localId) { mutableStateOf(false) }
    fun save(next: TileBackground) { saveError = !table.saveAccount(player.localId, player.name, prefs.copy(tileBackground = next)) }
    val hue = playerColor(prefs.color, palette.avatarColor(1))
    Box(Modifier.fillMaxWidth().height(96.dp)) {
        PlayerTileBackground(background, hue, Modifier.fillMaxSize())
        Text(player.name, color = palette.text, modifier = Modifier.padding(12.dp))
    }
    FlowRow(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
        TileBackgroundStyle.entries.forEach { style ->
            FilterChip(selected = background.style == style, onClick = { save(background.copy(style = style)) },
                label = { Text(style.label) }, enabled = style != TileBackgroundStyle.IMAGE || background.image != null)
        }
    }
    ColorPicker(background.color, { chosen ->
        // A selected image covers the color. Switch to a color style, retaining the image for later.
        save(background.copy(color = chosen, style = if (background.style == TileBackgroundStyle.IMAGE)
            TileBackgroundStyle.SOLID else background.style))
    }, hue)
    if (saveError) Text("Couldn’t save. Try again.", color = palette.warn)
    AvatarUploadControls(current = Personalization(null, 0, true,
        customAvatar = if (background.image == null) "" else "local-background", uploadApprovalRequired = false),
        busy = false,
        upload = { bytes, _ -> save(background.copy(style = TileBackgroundStyle.IMAGE,
            image = Base64.getEncoder().encodeToString(bytes))) },
        remove = { save(background.copy(style = TileBackgroundStyle.DEFAULT, image = null)) },
        localOnly = true, imageKind = "tile background")
    ToneButton("Reset tile background", { save(TileBackground()) }, enabled = background != TileBackground())
}
