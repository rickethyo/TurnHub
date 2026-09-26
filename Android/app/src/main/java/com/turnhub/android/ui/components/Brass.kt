package com.turnhub.android.ui.components

import androidx.compose.animation.core.LinearEasing
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.infiniteRepeatable
import androidx.compose.animation.core.rememberInfiniteTransition
import androidx.compose.animation.core.animateFloat
import androidx.compose.animation.core.tween
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.RowScope
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.drawBehind
import androidx.compose.ui.draw.rotate
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.drawscope.rotate
import androidx.compose.ui.semantics.clearAndSetSemantics
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.turnhub.android.protocol.AvatarIcon
import com.turnhub.android.ui.theme.TurnHubPalette
import com.turnhub.android.ui.theme.palette
import kotlin.math.PI
import kotlin.math.cos
import kotlin.math.sin

/** Screen background: the portal's two soft glows over the theme's base color. */
fun Modifier.tableBackground(p: TurnHubPalette): Modifier = drawBehind {
    drawRect(p.bg)
    drawRect(
        Brush.radialGradient(
            listOf(p.glowA, Color.Transparent),
            center = Offset(size.width * .12f, -size.height * .05f),
            radius = size.maxDimension * .7f,
        ),
    )
    drawRect(
        Brush.radialGradient(
            listOf(p.glowB, Color.Transparent),
            center = Offset(size.width, size.height * .04f),
            radius = size.maxDimension * .6f,
        ),
    )
}

/** A brass panel: soft top sheen, hairline border and, where the theme has them, corner rivets. */
@Composable
fun BrassCard(
    modifier: Modifier = Modifier,
    highlight: Color? = null,
    contentPadding: PaddingValues = PaddingValues(18.dp),
    content: @Composable ColumnScope.() -> Unit,
) {
    val p = palette
    val shape = RoundedCornerShape(18.dp)
    Column(
        modifier = modifier
            .fillMaxWidth()
            .clip(shape)
            .background(p.surface)
            .drawBehind {
                drawRect(
                    Brush.verticalGradient(
                        0f to p.text.copy(alpha = if (p.dark) .045f else .5f),
                        .38f to Color.Transparent,
                    ),
                )
                if (p.rivets) drawRivets(p)
            }
            .border(BorderStroke(if (highlight != null) 2.dp else 1.dp, highlight ?: p.line), shape)
            .padding(contentPadding),
        verticalArrangement = Arrangement.spacedBy(12.dp),
        content = content,
    )
}

private fun DrawScope.drawRivets(p: TurnHubPalette) {
    val inset = 10.dp.toPx()
    val r = 2.6.dp.toPx()
    listOf(
        Offset(inset, inset), Offset(size.width - inset, inset),
        Offset(inset, size.height - inset), Offset(size.width - inset, size.height - inset),
    ).forEach { c ->
        drawCircle(Brush.radialGradient(listOf(p.accentHi, p.accentLo), center = c, radius = r), r, c)
    }
}

/** The small uppercase gold label with a fading rule, as on every portal card. */
@Composable
fun Eyebrow(text: String, modifier: Modifier = Modifier, trailing: @Composable RowScope.() -> Unit = {}) {
    val p = palette
    Row(modifier = modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
        Text(
            text.uppercase(),
            color = if (p.dark) p.accentHi else p.accent,
            style = MaterialTheme.typography.labelMedium.copy(fontWeight = FontWeight.Bold, letterSpacing = 2.sp),
        )
        Spacer(Modifier.width(10.dp))
        Box(
            Modifier
                .weight(1f)
                .height(1.dp)
                .background(Brush.horizontalGradient(listOf(p.lineStrong, Color.Transparent))),
        )
        trailing()
    }
}

enum class Tone { NEUTRAL, GOOD, WARN, BAD, INFO, ACTIVE, ACCENT }

@Composable
fun toneColor(tone: Tone): Color {
    val p = palette
    return when (tone) {
        Tone.NEUTRAL -> p.muted
        Tone.GOOD -> p.good
        Tone.WARN -> p.warn
        Tone.BAD -> p.bad
        Tone.INFO -> p.info
        Tone.ACTIVE -> p.active
        Tone.ACCENT -> p.accent
    }
}

/** A status pill: always words, the dot and color only reinforce them. */
@Composable
fun StatusBadge(text: String, tone: Tone = Tone.NEUTRAL, modifier: Modifier = Modifier) {
    val p = palette
    val color = toneColor(tone)
    Row(
        modifier = modifier
            .clip(RoundedCornerShape(99.dp))
            .background(p.inset)
            .border(1.dp, if (tone == Tone.NEUTRAL) p.lineStrong else color.copy(alpha = .5f), RoundedCornerShape(99.dp))
            .padding(horizontal = 10.dp, vertical = 4.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(6.dp),
    ) {
        Box(Modifier.size(7.dp).clip(CircleShape).background(color))
        Text(text, color = color, style = MaterialTheme.typography.labelMedium.copy(fontWeight = FontWeight.Bold))
    }
}

/** The gold primary button. */
@Composable
fun AccentButton(
    text: String,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
    enabled: Boolean = true,
) {
    val p = palette
    val shape = RoundedCornerShape(12.dp)
    Button(
        onClick = onClick,
        enabled = enabled,
        shape = shape,
        contentPadding = PaddingValues(0.dp),
        colors = ButtonDefaults.buttonColors(
            containerColor = Color.Transparent,
            contentColor = p.onAccent,
            disabledContainerColor = p.surface2,
            disabledContentColor = p.faint,
        ),
        modifier = modifier.heightIn(min = 48.dp),
    ) {
        Box(
            Modifier
                .fillMaxWidth()
                .heightIn(min = 48.dp)
                .then(if (enabled) Modifier.background(p.accentBrush, shape) else Modifier)
                .padding(horizontal = 16.dp, vertical = 12.dp),
            contentAlignment = Alignment.Center,
        ) {
            Text(text, fontWeight = FontWeight.Bold, textAlign = TextAlign.Center)
        }
    }
}

/** An outlined button in one of the portal's tones (good / warn / bad / info). */
@Composable
fun ToneButton(
    text: String,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
    tone: Tone = Tone.NEUTRAL,
    enabled: Boolean = true,
) {
    val p = palette
    val color = if (tone == Tone.NEUTRAL) p.text else toneColor(tone)
    OutlinedButton(
        onClick = onClick,
        enabled = enabled,
        shape = RoundedCornerShape(12.dp),
        border = BorderStroke(1.dp, if (enabled) (if (tone == Tone.NEUTRAL) p.lineStrong else color.copy(alpha = .7f)) else p.line),
        colors = ButtonDefaults.outlinedButtonColors(
            containerColor = p.surface2,
            contentColor = color,
            disabledContainerColor = p.surface2.copy(alpha = .5f),
            disabledContentColor = p.faint,
        ),
        contentPadding = PaddingValues(horizontal = 14.dp, vertical = 10.dp),
        modifier = modifier.heightIn(min = 48.dp),
    ) {
        Text(text, fontWeight = FontWeight.SemiBold, textAlign = TextAlign.Center)
    }
}

/** A label/value row with a dashed-look divider, like the portal's status rows. */
@Composable
fun StatusRow(label: String, value: String) {
    val p = palette
    Row(
        Modifier
            .fillMaxWidth()
            .drawBehind {
                val y = size.height
                var x = 0f
                val dash = 4.dp.toPx()
                while (x < size.width) {
                    drawLine(p.line, Offset(x, y), Offset((x + dash).coerceAtMost(size.width), y), 1.dp.toPx())
                    x += dash * 2
                }
            }
            .padding(vertical = 9.dp),
        horizontalArrangement = Arrangement.SpaceBetween,
    ) {
        Text(label, color = p.muted, style = MaterialTheme.typography.bodyMedium)
        Spacer(Modifier.width(12.dp))
        Text(value, color = p.text, style = MaterialTheme.typography.bodyMedium.copy(fontWeight = FontWeight.SemiBold), textAlign = TextAlign.End)
    }
}

/** A small inset metric tile (label over a big number). */
@Composable
fun Metric(label: String, value: String, modifier: Modifier = Modifier) {
    val p = palette
    Column(
        modifier
            .clip(RoundedCornerShape(12.dp))
            .background(p.inset)
            .border(1.dp, p.line, RoundedCornerShape(12.dp))
            .padding(horizontal = 12.dp, vertical = 10.dp),
    ) {
        Text(label.uppercase(), color = p.faint, style = MaterialTheme.typography.labelSmall.copy(letterSpacing = 1.5.sp))
        Text(value, color = p.text, style = MaterialTheme.typography.titleLarge, maxLines = 1)
    }
}

/**
 * The TurnHub gear: drawn from plain geometry (the portal's mark). It turns
 * slowly while a game runs unless motion is reduced.
 */
@Composable
fun GearMark(size: Dp, modifier: Modifier = Modifier, spinning: Boolean = false, reduceMotion: Boolean = false) {
    val p = palette
    val angle = if (spinning && !reduceMotion) {
        val t = rememberInfiniteTransition(label = "gear")
        t.animateFloat(0f, 360f, infiniteRepeatable(tween(14_000, easing = LinearEasing)), label = "gearAngle").value
    } else 0f
    Canvas(modifier.size(size).rotate(angle).clearAndSetSemantics { }) {
        drawGear(p.accentBrush)
    }
}

private fun DrawScope.drawGear(brush: Brush) {
    val c = center
    val outer = size.minDimension * .47f
    val body = size.minDimension * .38f
    val hole = size.minDimension * .15f
    val teeth = 12
    val path = Path()
    for (i in 0 until teeth * 2) {
        val a0 = (i.toFloat() / (teeth * 2)) * 2 * PI.toFloat() - PI.toFloat() / 2
        val a1 = ((i + 1).toFloat() / (teeth * 2)) * 2 * PI.toFloat() - PI.toFloat() / 2
        val r = if (i % 2 == 0) outer else body
        val p0 = Offset(c.x + r * cos(a0), c.y + r * sin(a0))
        val p1 = Offset(c.x + r * cos(a1), c.y + r * sin(a1))
        if (i == 0) path.moveTo(p0.x, p0.y) else path.lineTo(p0.x, p0.y)
        path.lineTo(p1.x, p1.y)
    }
    path.close()
    drawPath(path, brush)
    drawCircle(Color.Black.copy(alpha = .0f), hole, c)
    drawCircle(brush, hole, c, style = Stroke(width = size.minDimension * .04f))
    drawCircle(Color(0x66000000), hole * .92f, c)
}

/**
 * A player's avatar: their preset icon, or initials, on the player's hue.
 * Decorative; the name is always shown beside it.
 */
@Composable
fun PlayerAvatar(
    name: String,
    playerNumber: Int,
    icon: AvatarIcon?,
    modifier: Modifier = Modifier,
    size: Dp = 44.dp,
) {
    val p = palette
    Box(
        modifier
            .size(size)
            .clip(CircleShape)
            .background(p.avatarColor(playerNumber))
            .border(2.dp, p.lineStrong, CircleShape)
            .clearAndSetSemantics { },
        contentAlignment = Alignment.Center,
    ) {
        if (icon != null) {
            AvatarGlyph(icon = icon, color = p.avatarText, size = size * .62f)
        } else {
            Text(
                initials(name),
                color = p.avatarText,
                style = MaterialTheme.typography.titleMedium.copy(fontSize = (size.value * .36f).sp),
            )
        }
    }
}

fun initials(name: String): String {
    val words = name.trim().split(Regex("\\s+")).filter { it.isNotEmpty() }
    if (words.isEmpty()) return "?"
    val first = words.first().first()
    val last = if (words.size > 1) words.last().first().toString() else ""
    return (first + last).uppercase()
}

/**
 * The portal's brass dial: a 270° track, a gold arc for [fraction] and a
 * needle, with [value] and [caption] in the middle. [fraction] null draws the
 * dial at rest. The arc is decorative: the readout carries the meaning.
 */
@Composable
fun Gauge(
    value: String,
    caption: String,
    fraction: Float?,
    modifier: Modifier = Modifier,
    size: Dp = 180.dp,
    arcColor: Color? = null,
    reduceMotion: Boolean = false,
) {
    val p = palette
    val target = (fraction ?: 0f).coerceIn(0f, 1f)
    val animated by animateFloatAsState(
        target,
        animationSpec = if (reduceMotion) tween(0) else tween(600),
        label = "gauge",
    )
    val arc = arcColor ?: p.accent
    Box(modifier.size(size), contentAlignment = Alignment.Center) {
        Canvas(Modifier.size(size).clearAndSetSemantics { }) {
            val s = this.size.minDimension
            val c = center
            // Bezel and face.
            drawCircle(Brush.verticalGradient(listOf(p.accentHi, p.accent, p.accentLo)), s * .49f, c)
            drawCircle(Brush.radialGradient(listOf(p.surface3, p.inset), c, s * .45f), s * .44f, c)
            // Ticks.
            for (i in 0..27) {
                val a = Math.toRadians(135.0 + i * 10.0).toFloat()
                val major = i % 3 == 0
                val r0 = s * (if (major) .36f else .385f)
                val r1 = s * .41f
                drawLine(
                    if (major) p.accentHi.copy(alpha = .8f) else p.faint.copy(alpha = .6f),
                    Offset(c.x + r0 * cos(a), c.y + r0 * sin(a)),
                    Offset(c.x + r1 * cos(a), c.y + r1 * sin(a)),
                    strokeWidth = if (major) 2.dp.toPx() else 1.dp.toPx(),
                )
            }
            val arcR = s * .33f
            val topLeft = Offset(c.x - arcR, c.y - arcR)
            val arcSize = Size(arcR * 2, arcR * 2)
            val stroke = Stroke(width = s * .045f, cap = StrokeCap.Round)
            drawArc(p.line, 135f, 270f, false, topLeft, arcSize, style = stroke)
            if (animated > 0f) drawArc(arc, 135f, 270f * animated, false, topLeft, arcSize, style = stroke)
            // Needle.
            rotate(degrees = -135f + 270f * animated, pivot = c) {
                val needle = Path().apply {
                    moveTo(c.x, c.y - s * .31f)
                    lineTo(c.x + s * .022f, c.y)
                    lineTo(c.x, c.y + s * .07f)
                    lineTo(c.x - s * .022f, c.y)
                    close()
                }
                drawPath(needle, p.accentHi)
            }
            drawCircle(Brush.verticalGradient(listOf(p.accentHi, p.accentLo)), s * .05f, c)
            drawCircle(p.inset, s * .018f, c)
        }
        Column(
            Modifier.padding(top = size * .42f),
            horizontalAlignment = Alignment.CenterHorizontally,
        ) {
            Text(
                value,
                color = p.text,
                style = MaterialTheme.typography.headlineSmall.copy(fontSize = (size.value * .12f).sp),
                maxLines = 1,
            )
            Text(caption.uppercase(), color = p.muted, style = MaterialTheme.typography.labelSmall.copy(letterSpacing = 1.5.sp))
        }
    }
}

/** A thin draining bar for countdowns (pass grace, life-request approval). Decorative. */
@Composable
fun CountdownBar(fraction: Float, color: Color, modifier: Modifier = Modifier) {
    val p = palette
    Box(
        modifier
            .fillMaxWidth()
            .height(4.dp)
            .clip(RoundedCornerShape(2.dp))
            .background(p.line)
            .clearAndSetSemantics { },
    ) {
        Box(
            Modifier
                .fillMaxWidth(fraction.coerceIn(0f, 1f))
                .height(4.dp)
                .background(color),
        )
    }
}
