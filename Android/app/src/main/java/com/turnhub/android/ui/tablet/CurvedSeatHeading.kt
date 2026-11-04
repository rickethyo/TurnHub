package com.turnhub.android.ui.tablet

import android.graphics.Paint
import android.graphics.Path
import android.graphics.PathMeasure
import androidx.compose.foundation.Canvas
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.nativeCanvas
import androidx.compose.ui.graphics.toArgb
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.core.content.res.ResourcesCompat
import com.turnhub.android.R
import com.turnhub.android.ui.theme.palette

/** Full names follow a shallow seat arc; the accessibility label stays ordinary text. */
@Composable
internal fun CurvedSeatHeading(name: String, subtitle: String?, modifier: Modifier = Modifier) {
    val context = LocalContext.current
    val density = LocalDensity.current
    val font = remember(context) { ResourcesCompat.getFont(context, R.font.inter_variable) }
    val ink = palette.text.toArgb()
    val shadow = palette.surface.toArgb()
    Canvas(modifier.semantics { contentDescription = listOfNotNull(name, subtitle).joinToString(", ") }) {
        val inset = 8 * density.density
        val width = (size.width - inset * 2).coerceAtLeast(1f)
        val bend = minOf(20 * density.density, width * .09f)
        val paint = Paint(Paint.ANTI_ALIAS_FLAG).apply { typeface = font }
        fun lines(text: String, fontSize: Int, bold: Boolean = false): List<String> {
            paint.isFakeBoldText = bold
            paint.textSize = fontSize * density.density * density.fontScale
            val result = mutableListOf<String>()
            var line = ""
            for (word in text.split(' ')) {
                val candidate = if (line.isEmpty()) word else "$line $word"
                if (line.isNotEmpty() && paint.measureText(candidate) > width) { result += line; line = word }
                else line = candidate
                while (paint.measureText(line) > width && line.length > 1) {
                    val count = paint.breakText(line, true, width, null).coerceAtLeast(1)
                    result += line.take(count); line = line.drop(count)
                }
            }
            if (line.isNotEmpty()) result += line
            return result
        }
        var titleSize = 14; var subtitleSize = 12
        outer@ for (title in 26 downTo 14) for (secondary in 20 downTo 12) {
            val titleLines = lines(name, title, true)
            val subtitleLines = subtitle?.let { lines(it, secondary) }.orEmpty()
            val height = (titleLines.size * title * 1.3f + subtitleLines.size * secondary * 1.3f) *
                density.density * density.fontScale + bend + inset * 2
            if (height <= size.height) { titleSize = title; subtitleSize = secondary; break@outer }
        }
        var top = inset
        fun drawLine(text: String, fontSize: Int, bold: Boolean) {
            paint.textSize = fontSize * density.density * density.fontScale
            paint.isFakeBoldText = bold
            val metrics = paint.fontMetrics
            val baseline = top - metrics.top
            val path = Path().apply {
                moveTo(inset, baseline)
                quadTo(size.width / 2, baseline + bend * 2, size.width - inset, baseline)
            }
            val length = PathMeasure(path, false).length
            val offset = ((length - paint.measureText(text)) / 2).coerceAtLeast(0f)
            paint.style = Paint.Style.STROKE; paint.strokeWidth = 4 * density.density; paint.color = shadow
            drawContext.canvas.nativeCanvas.drawTextOnPath(text, path, offset, 0f, paint)
            paint.style = Paint.Style.FILL; paint.color = ink
            drawContext.canvas.nativeCanvas.drawTextOnPath(text, path, offset, 0f, paint)
            top += metrics.bottom - metrics.top + density.density
        }
        for (line in lines(name, titleSize, true)) drawLine(line, titleSize, true)
        for (line in subtitle?.let { lines(it, subtitleSize) }.orEmpty()) drawLine(line, subtitleSize, false)
    }
}
