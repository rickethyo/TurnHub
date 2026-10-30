package com.turnhub.android.ui.components

import androidx.compose.animation.core.LinearEasing
import androidx.compose.animation.core.RepeatMode
import androidx.compose.animation.core.animateFloat
import androidx.compose.animation.core.infiniteRepeatable
import androidx.compose.animation.core.rememberInfiniteTransition
import androidx.compose.animation.core.tween
import androidx.compose.foundation.Canvas
import androidx.compose.runtime.Composable
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.PathEffect
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.drawscope.rotate
import androidx.compose.ui.semantics.clearAndSetSemantics
import androidx.compose.ui.unit.dp
import com.turnhub.android.ui.theme.palette

/** Decorative shared-table signal. Static means no animation clocks are created. */
@Composable
internal fun SignalBeacon(
    glow: Color,
    reduceMotion: Boolean,
    modifier: Modifier = Modifier,
    paused: Boolean = false,
) {
    val dark = palette.dark
    val still = reduceMotion || paused
    val transition = if (!still) rememberInfiniteTransition(label = "signal") else null
    val wave = transition?.animateFloat(0f, 1f,
        infiniteRepeatable(tween(3_200, easing = LinearEasing)), label = "wave") ?: rememberUpdatedState(0f)
    val turn = transition?.animateFloat(0f, 360f,
        infiniteRepeatable(tween(48_000, easing = LinearEasing)), label = "turn") ?: rememberUpdatedState(0f)
    val breath = transition?.animateFloat(.6f, 1f,
        infiniteRepeatable(tween(2_000), RepeatMode.Reverse), label = "breath") ?: rememberUpdatedState(.85f)
    // Read animation values during drawing: the surrounding artwork and text do not recompose per frame.
    Canvas(modifier.clearAndSetSemantics { }) {
        val c = Offset(size.width * .86f, size.height * .38f)
        val r = size.height * .5f
        drawCircle(Brush.radialGradient(listOf(glow.copy(alpha = .3f), Color.Transparent), c, r * 1.5f), r * 1.5f, c)
        repeat(3) { i ->
            val t = if (still) (i + 1) / 3.5f else (wave.value + i / 3f) % 1f
            drawCircle(glow.copy(alpha = .55f * (1f - t)), r * (.25f + 1.15f * t), c, style = Stroke(1.5.dp.toPx()))
        }
        rotate(turn.value, c) {
            drawCircle(glow.copy(alpha = .3f), r * .62f, c,
                style = Stroke(1.dp.toPx(), pathEffect = PathEffect.dashPathEffect(floatArrayOf(6f, 10f))))
        }
        drawCircle(Brush.radialGradient(listOf(glow.copy(alpha = breath.value), Color.Transparent), c, r * .22f), r * .22f, c)
        drawCircle(glow, r * .06f, c)
        drawRect(Brush.verticalGradient(0f to Color.White.copy(alpha = if (dark) .06f else .25f), .45f to Color.Transparent))
    }
}
