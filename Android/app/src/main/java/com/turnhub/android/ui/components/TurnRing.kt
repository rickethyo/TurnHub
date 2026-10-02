package com.turnhub.android.ui.components

import androidx.compose.animation.animateColorAsState
import androidx.compose.animation.core.Animatable
import androidx.compose.animation.core.FastOutSlowInEasing
import androidx.compose.animation.core.RepeatMode
import androidx.compose.animation.core.animateFloat
import androidx.compose.animation.core.infiniteRepeatable
import androidx.compose.animation.core.rememberInfiniteTransition
import androidx.compose.animation.core.spring
import androidx.compose.animation.core.tween
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.size
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.semantics.clearAndSetSemantics
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.turnhub.android.ui.theme.DesignTokens
import com.turnhub.android.ui.theme.palette
import com.turnhub.android.ui.theme.toTextStyle

/**
 * The modern turn hero: a full ring that drains (a turn timer) or fills (the
 * turn clock against the long-turn mark), with the clock in the middle. While
 * [live], a soft halo breathes behind it unless motion is reduced. The ring
 * is decorative; [value] and [caption] carry the meaning, and the caller
 * gives the whole hero its spoken description.
 */
@Composable
fun TurnRing(
    value: String,
    caption: String,
    fraction: Float?,
    color: Color,
    live: Boolean,
    modifier: Modifier = Modifier,
    size: Dp = 232.dp,
    reduceMotion: Boolean = false,
) {
    val p = palette
    val target = (fraction ?: 0f).coerceIn(0f, 1f)
    val progress = remember { Animatable(target) }
    LaunchedEffect(target, reduceMotion) {
        // A big jump (a new turn) springs; the per-second tick glides.
        if (reduceMotion) {
            progress.snapTo(target)
        } else if (kotlin.math.abs(progress.value - target) > .2f) {
            progress.animateTo(
                target,
                spring(DesignTokens.Motion.GENTLE_DAMPING, DesignTokens.Motion.GENTLE_STIFFNESS),
            )
        } else {
            progress.animateTo(target, tween(DesignTokens.Motion.SLOW_MS))
        }
    }
    val ringColor by animateColorAsState(color, tween(DesignTokens.Motion.BASE_MS), label = "ringColor")
    val breathe = if (live && !reduceMotion) {
        rememberInfiniteTransition(label = "halo").animateFloat(
            initialValue = .55f,
            targetValue = 1f,
            animationSpec = infiniteRepeatable(tween(2_400, easing = FastOutSlowInEasing), RepeatMode.Reverse),
            label = "haloAlpha",
        ).value
    } else if (live) .8f else 0f

    Box(modifier.size(size), contentAlignment = Alignment.Center) {
        Canvas(Modifier.size(size).clearAndSetSemantics { }) {
            val s = this.size.minDimension
            val c = center
            if (breathe > 0f) {
                drawCircle(
                    Brush.radialGradient(
                        listOf(ringColor.copy(alpha = .22f * breathe), Color.Transparent),
                        center = c,
                        radius = s * .5f,
                    ),
                    radius = s * .5f,
                    center = c,
                )
            }
            val stroke = s * .055f
            val r = s * .5f - stroke * 1.4f
            val topLeft = Offset(c.x - r, c.y - r)
            val arcSize = Size(r * 2, r * 2)
            drawArc(p.fillStrong, 0f, 360f, false, topLeft, arcSize, style = Stroke(stroke))
            val sweep = 360f * progress.value
            if (sweep > 0f) {
                drawArc(ringColor, -90f, sweep, false, topLeft, arcSize, style = Stroke(stroke, cap = StrokeCap.Round))
                // A bright head on the moving end, like a watch's seconds hand.
                val a = Math.toRadians((-90f + sweep).toDouble())
                drawCircle(
                    p.bg,
                    radius = stroke * .28f,
                    center = Offset(c.x + r * kotlin.math.cos(a).toFloat(), c.y + r * kotlin.math.sin(a).toFloat()),
                )
            }
        }
        Column(horizontalAlignment = Alignment.CenterHorizontally) {
            Text(
                value,
                color = p.text,
                style = DesignTokens.Type.clock.toTextStyle().copy(fontSize = (size.value * .22f).sp),
                maxLines = 1,
                textAlign = TextAlign.Center,
            )
            Text(
                caption.uppercase(),
                color = p.muted,
                style = DesignTokens.Type.eyebrow.toTextStyle(),
                textAlign = TextAlign.Center,
            )
        }
    }
}
