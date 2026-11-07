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
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.turnhub.android.protocol.AtlasLinks
import com.turnhub.android.ui.theme.palette

/**
 * Atlas's links beside its battery gauge, drawn like the Atlas screen's header
 * (owner, 2026-11-03): A its hotspot (shown only while up), H the home network
 * (slashed while Atlas isn't on it) and S the Sigil radio (three bars, lit
 * while a Sigil is online). Each has its own shape and letter as well as its
 * colour, so none relies on colour alone (Invariant 11).
 */
@Composable
fun AtlasLinkIcons(links: AtlasLinks, modifier: Modifier = Modifier) {
    val p = palette
    val description = buildString {
        append(if (links.hotspot) "Atlas hotspot on" else "Atlas hotspot off")
        append(if (links.home) ", on home Wi-Fi" else ", not on home Wi-Fi")
        append(", ${links.sigils} ${if (links.sigils == 1) "Sigil" else "Sigils"} online")
    }
    Row(
        modifier.semantics(mergeDescendants = true) { contentDescription = description },
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(6.dp),
    ) {
        if (links.hotspot) LinkIcon("Hotspot", p.accent, lit = true, slashed = false, bars = false)
        LinkIcon("Wi-Fi", p.info, lit = links.home, slashed = !links.home, bars = false)
        LinkIcon("${links.sigils} ${if (links.sigils == 1) "Sigil" else "Sigils"}", p.active, lit = links.sigils > 0, slashed = false, bars = true)
    }
}

@Composable
private fun LinkIcon(letter: String, color: Color, lit: Boolean, slashed: Boolean, bars: Boolean) {
    val p = palette
    val ink = if (lit) color else p.faint
    Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(2.dp)) {
        Canvas(Modifier.size(14.dp)) {
            val w = size.width
            val stroke = 1.8.dp.toPx()
            if (bars) {
                val barW = w / 5
                for (i in 0..2) {
                    val h = size.height * (.35f + i * .3f).coerceAtMost(1f)
                    drawRoundRect(ink, topLeft = Offset(i * barW * 2, size.height - h), size = Size(barW, h),
                        cornerRadius = CornerRadius(barW / 3))
                }
            } else {
                val centre = Offset(w / 2, size.height * .92f)
                for (r in listOf(w * .42f, w * .74f)) {
                    drawArc(ink, startAngle = 225f, sweepAngle = 90f, useCenter = false,
                        topLeft = Offset(centre.x - r, centre.y - r), size = Size(2 * r, 2 * r),
                        style = Stroke(stroke, cap = StrokeCap.Round))
                }
                drawCircle(ink, radius = stroke, center = Offset(centre.x, centre.y - stroke / 2))
            }
            if (slashed) drawLine(p.muted, Offset(0f, 0f), Offset(w, size.height), strokeWidth = stroke, cap = StrokeCap.Round)
        }
        Text(letter, color = ink, style = MaterialTheme.typography.labelSmall.copy(fontWeight = FontWeight.Bold))
    }
}
