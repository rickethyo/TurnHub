package com.turnhub.android.ui.tablet

import android.graphics.BitmapFactory
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
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
internal fun PlayerTileBackground(background: TileBackground, seatColor: Color, modifier: Modifier = Modifier) {
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
        if (bitmap != null) {
            Image(bitmap, contentDescription = null, modifier = Modifier.fillMaxSize(), contentScale = ContentScale.Crop)
            Box(Modifier.fillMaxSize().background(p.surface.copy(alpha = .82f)))
        }
    }
}
