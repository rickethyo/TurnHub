package com.turnhub.android.ui.tablet

import android.graphics.BitmapFactory
import androidx.compose.foundation.Image
import androidx.compose.foundation.gestures.detectTransformGestures
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clipToBounds
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalUriHandler
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import com.turnhub.android.domain.InvestigatorCardCatalogue
import com.turnhub.android.ui.components.ToneButton
import com.turnhub.android.ui.theme.palette

/** References are intentionally opened, never downloaded for every search result. */
@Composable
internal fun InvestigatorCardReferenceButton(catalogueId: String?) {
    val entry = InvestigatorCardCatalogue.find(catalogueId) ?: return
    val uri = LocalUriHandler.current
    var opened by remember(catalogueId) { mutableStateOf(false) }
    if (entry.imageId != null) {
        ToneButton("View card reference", { opened = true })
        if (opened) InvestigatorCardReference(entry.name, entry.imageId) { opened = false }
    } else {
        Text("No card image in this catalogue. Add rules or notes from your card.", color = palette.muted,
            style = MaterialTheme.typography.bodySmall)
    }
    TextButton(onClick = { uri.openUri(entry.sourceUrl) }) { Text("Community source") }
}

@Composable
private fun InvestigatorCardReference(name: String, imageId: String, onDismiss: () -> Unit) {
    val context = LocalContext.current
    val uri = LocalUriHandler.current
    var image by remember(imageId) { mutableStateOf<ImageBitmap?>(null) }
    var loading by remember(imageId) { mutableStateOf(true) }
    var attempt by remember(imageId) { mutableStateOf(0) }
    var zoom by remember(imageId) { mutableStateOf(1f) }
    var pan by remember(imageId) { mutableStateOf(Offset.Zero) }
    LaunchedEffect(imageId, attempt) {
        loading = true
        val bytes = CommanderArtCache.source(context).investigatorCard(imageId)
        image = bytes?.let { BitmapFactory.decodeByteArray(it, 0, it.size)?.asImageBitmap() }
        loading = false
    }
    Dialog(onDismissRequest = onDismiss, properties = DialogProperties(usePlatformDefaultWidth = false)) {
        Surface(Modifier.fillMaxSize(), color = palette.surface) {
            Column(Modifier.fillMaxSize().padding(12.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                Text(name, style = MaterialTheme.typography.titleMedium, color = palette.text)
                Text("Card reference · © CMON · BoardGameGeek\nMay show paired cards or both relic faces. Pinch and drag to read.",
                    color = palette.muted, style = MaterialTheme.typography.bodySmall)
                Box(Modifier.fillMaxWidth().weight(1f).clipToBounds(), contentAlignment = Alignment.Center) {
                    val loaded = image
                    if (loaded != null) Image(loaded, contentDescription = "$name card reference. Use zoom controls to enlarge.",
                        contentScale = ContentScale.Fit, modifier = Modifier.fillMaxSize()
                            .pointerInput(imageId) {
                                detectTransformGestures { _, move, scale, _ ->
                                    zoom = (zoom * scale).coerceIn(1f, 6f)
                                    pan = if (zoom == 1f) Offset.Zero else pan + move
                                }
                            }.graphicsLayer(scaleX = zoom, scaleY = zoom, translationX = pan.x, translationY = pan.y))
                    else Column(horizontalAlignment = Alignment.CenterHorizontally) {
                        Text(if (loading) "Loading card reference…" else "Card image unavailable. Connect to download it, or use your printed card.",
                            color = palette.text)
                        if (!loading) TextButton(onClick = { attempt++ }) { Text("Retry") }
                    }
                }
                Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceEvenly) {
                    TextButton(enabled = image != null && zoom > 1f, onClick = { zoom = (zoom / 1.5f).coerceAtLeast(1f); if (zoom == 1f) pan = Offset.Zero }) { Text("Zoom out") }
                    TextButton(onClick = { zoom = 1f; pan = Offset.Zero }) { Text("Fit") }
                    TextButton(enabled = image != null && zoom < 6f, onClick = { zoom = (zoom * 1.5f).coerceAtMost(6f) }) { Text("Zoom in") }
                }
                Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                    TextButton(onClick = { uri.openUri(InvestigatorCardCatalogue.imageSource(imageId)) }) { Text("Source") }
                    TextButton(onClick = onDismiss) { Text("Close") }
                }
            }
        }
    }
}
