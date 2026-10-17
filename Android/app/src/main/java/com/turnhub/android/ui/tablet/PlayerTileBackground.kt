package com.turnhub.android.ui.tablet

import android.graphics.BitmapFactory
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.ui.Alignment
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.layout.ContentScale
import com.turnhub.android.domain.TileBackground
import com.turnhub.android.domain.TileBackgroundStyle
import com.turnhub.android.ui.theme.palette
import java.util.Base64

/** Passive decorative layer shared by setup and live panels; controls stay above it. */
@Composable
internal fun PlayerTileBackground(background: TileBackground, seatColor: Color, modifier: Modifier = Modifier,
    /** A named commander's card art replaces the chosen background, with its artist credit. */
    art: TileArt? = null,
    /** Too-small tiles (the setup star) credit the artist beside the player instead. */
    showCredit: Boolean = true) {
    val p = palette
    val hue = background.color?.let { Color(android.graphics.Color.parseColor(it)) } ?: seatColor
    val bitmap = remember(background.style, background.image) {
        if (background.style != TileBackgroundStyle.IMAGE) null else runCatching {
            val bytes = Base64.getDecoder().decode(background.image)
            val bounds = BitmapFactory.Options().apply { inJustDecodeBounds = true }
            BitmapFactory.decodeByteArray(bytes, 0, bytes.size, bounds)
            if (bounds.outWidth in 32..512 && bounds.outHeight == bounds.outWidth)
                BitmapFactory.decodeByteArray(bytes, 0, bytes.size)?.asImageBitmap() else null
        }.getOrNull()
    }
    val brush = when (background.style) {
        TileBackgroundStyle.SOLID -> androidx.compose.ui.graphics.SolidColor(hue.copy(alpha = if (p.dark) .4f else .3f))
        TileBackgroundStyle.GRADIENT -> Brush.linearGradient(listOf(hue.copy(alpha = .55f), p.surface, hue.copy(alpha = .2f)))
        else -> Brush.verticalGradient(listOf(hue.copy(alpha = if (p.dark) .30f else .22f),
            hue.copy(alpha = if (p.dark) .52f else .40f)))
    }
    Box(modifier.background(p.surface).background(brush)) {
        if (art != null) {
            Image(art.image, contentDescription = null, modifier = Modifier.fillMaxSize(), contentScale = ContentScale.Crop)
            // Dim enough for the life total and controls to stay readable over any art.
            Box(Modifier.fillMaxSize().background(p.surface.copy(alpha = .55f)))
            // Kept to the panel's outer third so a long credit ends before the centre controls.
            if (showCredit) Text("Art: ${art.artist}", color = p.text.copy(alpha = .75f), style = MaterialTheme.typography.labelSmall,
                maxLines = 1, overflow = TextOverflow.Ellipsis, textAlign = TextAlign.End,
                modifier = Modifier.align(Alignment.TopEnd).fillMaxWidth(.34f).padding(horizontal = 10.dp, vertical = 4.dp)
                    .semantics { contentDescription = "${art.cardName} art by ${art.artist}, from ${art.source}" })
        } else if (bitmap != null) {
            Image(bitmap, contentDescription = null, modifier = Modifier.fillMaxSize(), contentScale = ContentScale.Crop)
            Box(Modifier.fillMaxSize().background(p.surface.copy(alpha = .82f)))
        }
    }
}
