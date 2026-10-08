package com.turnhub.android.ui.components

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.size
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.turnhub.android.protocol.AtlasBattery
import com.turnhub.android.ui.theme.palette

/**
 * Atlas's battery, drawn like the Atlas screen's header gauge: the percent,
 * then a phone-style cell filled to the charge, with a bolt while charging.
 * The fill takes the theme's good / warn / bad colors, but the number and the
 * bolt carry the state, so it never relies on color alone (Invariant 11).
 */
@Composable
fun AtlasBatteryGauge(battery: AtlasBattery, modifier: Modifier = Modifier) {
    val p = palette
    val fill = when {
        battery.low -> p.bad
        battery.charging -> p.good
        battery.percent <= 35 -> p.warn
        else -> p.good
    }
    val description = buildString {
        append("Atlas battery ${battery.percent} percent")
        if (battery.charging) append(", charging")
        if (battery.low) append(", low")
    }
    Row(
        modifier.semantics(mergeDescendants = true) { contentDescription = description },
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(4.dp),
    ) {
        Text(
            "${battery.percent}%",
            color = if (battery.low) p.bad else p.muted,
            style = MaterialTheme.typography.labelMedium.copy(fontWeight = FontWeight.Bold),
        )
        Canvas(Modifier.size(width = 25.dp, height = 12.dp)) {
            val stroke = 1.5.dp.toPx()
            val nubW = 2.dp.toPx()
            val bodyW = size.width - nubW - stroke
            val corner = CornerRadius(3.dp.toPx())
            // Outline and the terminal nub.
            drawRoundRect(
                color = p.muted,
                topLeft = Offset(stroke / 2, stroke / 2),
                size = Size(bodyW, size.height - stroke),
                cornerRadius = corner,
                style = Stroke(stroke),
            )
            drawRoundRect(
                color = p.muted,
                topLeft = Offset(bodyW + stroke / 2, size.height * .3f),
                size = Size(nubW, size.height * .4f),
                cornerRadius = CornerRadius(1.dp.toPx()),
            )
            // The charge.
            val inset = stroke + 1.dp.toPx()
            val innerW = bodyW - 2 * inset + stroke
            val level = (battery.percent.coerceIn(0, 100) / 100f).let { if (battery.percent > 0) it.coerceAtLeast(.08f) else 0f }
            if (level > 0f) {
                drawRoundRect(
                    color = fill,
                    topLeft = Offset(inset, inset),
                    size = Size(innerW * level, size.height - 2 * inset),
                    cornerRadius = CornerRadius(1.5.dp.toPx()),
                )
            }
            if (battery.charging) {
                // A bolt across the cell, outlined in the background so it reads on any fill.
                val cx = (stroke / 2 + bodyW / 2)
                val h = size.height
                val bolt = Path().apply {
                    moveTo(cx + h * .10f, h * .08f)
                    lineTo(cx - h * .32f, h * .56f)
                    lineTo(cx - h * .02f, h * .56f)
                    lineTo(cx - h * .10f, h * .92f)
                    lineTo(cx + h * .32f, h * .44f)
                    lineTo(cx + h * .02f, h * .44f)
                    close()
                }
                drawPath(bolt, color = p.bg, style = Stroke(1.5.dp.toPx()))
                drawPath(bolt, color = p.text)
            }
        }
    }
}
