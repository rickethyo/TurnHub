package com.turnhub.android.ui.tablet

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.text.font.FontStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.rememberTextMeasurer
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.turnhub.android.domain.TablePlayer
import com.turnhub.android.ui.components.AvatarGlyph
import com.turnhub.android.ui.theme.palette

internal data class PanelMarker(val text: String, val background: Color, val color: Color)

/** Names keep their natural width up to the existing cap; status uses what is actually left. */
@Composable
internal fun AdaptivePlayerHeading(
    members: List<TablePlayer>, name: String, subtitle: String?, markers: List<PanelMarker>,
    nameMax: Dp, panelWidth: Dp, panelHeight: Dp, reserveCredit: Boolean, modifier: Modifier = Modifier,
) {
    val p = palette
    val measure = rememberTextMeasurer()
    val density = LocalDensity.current
    val nameStyle = MaterialTheme.typography.titleMedium.copy(fontWeight = FontWeight.SemiBold)
    val subtitleStyle = MaterialTheme.typography.labelMedium.copy(fontStyle = FontStyle.Italic)
    val markerStyle = MaterialTheme.typography.labelMedium.copy(fontWeight = FontWeight.Bold)
    BoxWithConstraints(modifier.fillMaxWidth()) {
        val width = constraints.maxWidth
        val gap = with(density) { 8.dp.roundToPx() }
        val avatars = members.mapNotNull { it.avatar }
        val avatarWidth = avatars.size * with(density) { 40.dp.roundToPx() }
        val naturalName = maxOf(measure.measure(name, nameStyle, softWrap = false).size.width,
            subtitle?.let { measure.measure(it, subtitleStyle, softWrap = false).size.width } ?: 0)
        val nameWidth = minOf(naturalName, with(density) { nameMax.roundToPx() }, (width - avatarWidth).coerceAtLeast(1))
        val remaining = (width - avatarWidth - nameWidth - gap).coerceAtLeast(0)
        val minMarkers = markers.sumOf {
            measure.measure(it.text, markerStyle.copy(fontSize = 12.sp), softWrap = false).size.width + 2 * gap
        } + (markers.size - 1).coerceAtLeast(0) * gap
        val inline = markers.isEmpty() || minMarkers <= remaining
        val targetSize = (minOf(panelWidth.value, panelHeight.value) * .08f).toInt().coerceIn(12, 24)
        val names: @Composable () -> Unit = {
            Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                avatars.forEach { AvatarGlyph(it, p.text, 32.dp) }
                Column(Modifier.width(with(density) { nameWidth.toDp() })) {
                    Text(name, color = p.text, style = nameStyle, maxLines = 1, overflow = TextOverflow.Ellipsis)
                    subtitle?.let { Text(it, color = p.muted, style = subtitleStyle, maxLines = 1, overflow = TextOverflow.Ellipsis) }
                }
            }
        }
        if (inline) Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            names()
            if (markers.isNotEmpty()) AdaptivePanelMarkers(markers, targetSize,
                Modifier.weight(1f).padding(top = if (reserveCredit) 12.dp else 0.dp))
        } else Column(Modifier.fillMaxWidth(), verticalArrangement = Arrangement.spacedBy(8.dp)) {
            names()
            AdaptivePanelMarkers(markers, targetSize)
        }
    }
}

/** Fit the complete badge row before shrinking; a crowded row wraps instead of losing status words. */
@Composable
internal fun AdaptivePanelMarkers(markers: List<PanelMarker>, targetSize: Int = 24, modifier: Modifier = Modifier) {
    if (markers.isEmpty()) return
    val measure = rememberTextMeasurer()
    val density = LocalDensity.current
    val style = MaterialTheme.typography.labelMedium.copy(fontWeight = FontWeight.Bold)
    BoxWithConstraints(modifier.fillMaxWidth()) {
        val gap = with(density) { 8.dp.roundToPx() }
        val cap = targetSize.coerceIn(12, 24)
        val size = (cap downTo 12).firstOrNull { font ->
            markers.sumOf { measure.measure(it.text, style.copy(fontSize = font.sp), softWrap = false).size.width + 2 * gap } +
                (markers.size - 1) * gap <= constraints.maxWidth
        } ?: 12
        FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
            markers.forEach { marker ->
                Text(marker.text, color = marker.color, style = style.copy(fontSize = size.sp, lineHeight = (size * 1.2f).sp),
                    modifier = Modifier.background(marker.background, RoundedCornerShape(8.dp))
                        .padding(horizontal = 8.dp, vertical = if (size > 16) 4.dp else 2.dp))
            }
        }
    }
}
