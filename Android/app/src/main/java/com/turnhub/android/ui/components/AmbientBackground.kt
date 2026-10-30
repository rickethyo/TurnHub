package com.turnhub.android.ui.components

import androidx.compose.animation.core.FastOutSlowInEasing
import androidx.compose.animation.core.LinearEasing
import androidx.compose.animation.core.RepeatMode
import androidx.compose.animation.core.animateFloat
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.infiniteRepeatable
import androidx.compose.animation.core.rememberInfiniteTransition
import androidx.compose.animation.core.snap
import androidx.compose.animation.core.tween
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxScope
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.staticCompositionLocalOf
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.drawscope.clipRect
import androidx.compose.ui.graphics.drawscope.inset
import androidx.compose.ui.graphics.drawscope.rotate
import androidx.compose.ui.semantics.clearAndSetSemantics
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.compose.LocalLifecycleOwner
import com.turnhub.android.ui.theme.palette

/** Embedded pages keep the shell's single background rather than painting or animating another one. */
internal val LocalAmbientBackgroundPresent = staticCompositionLocalOf { false }

/** Warm light behind opaque cards; reduced motion and backgrounding create no animation clocks. */
@Composable
internal fun AmbientBackground(
    reduceMotion: Boolean,
    modifier: Modifier = Modifier,
    quiet: Boolean = false,
    contentAlignment: Alignment = Alignment.TopStart,
    content: @Composable BoxScope.() -> Unit,
) {
    if (LocalAmbientBackgroundPresent.current) {
        Box(modifier, contentAlignment = contentAlignment, content = content)
        return
    }
    val p = palette
    val highContrast = p.bg == Color.Black
    val lifecycle by LocalLifecycleOwner.current.lifecycle.currentStateFlow.collectAsState()
    val moving = !reduceMotion && !highContrast && lifecycle.isAtLeast(Lifecycle.State.RESUMED)
    val transition = if (moving) rememberInfiniteTransition(label = "ambientWarmth") else null
    val amber = transition?.animateFloat(0f, 1f, infiniteRepeatable(
        tween(11_000, easing = FastOutSlowInEasing), RepeatMode.Reverse), label = "amber") ?: rememberUpdatedState(.4f)
    val copper = transition?.animateFloat(1f, 0f, infiniteRepeatable(
        tween(17_000, easing = FastOutSlowInEasing), RepeatMode.Reverse), label = "copper") ?: rememberUpdatedState(.65f)
    val gold = transition?.animateFloat(0f, 1f, infiniteRepeatable(
        tween(13_000, easing = FastOutSlowInEasing), RepeatMode.Reverse), label = "gold") ?: rememberUpdatedState(.3f)
    val rotation = transition?.animateFloat(0f, 360f, infiniteRepeatable(
        tween(240_000, easing = LinearEasing)), label = "gears") ?: rememberUpdatedState(12f)
    val strength = animateFloatAsState(if (quiet) .3f else 1f,
        animationSpec = if (moving) tween(1_500) else snap(), label = "ambientStrength")
    Box(modifier.background(p.bg), contentAlignment = contentAlignment) {
        if (!highContrast) Canvas(Modifier.matchParentSize().clearAndSetSemantics { }) {
            val base = if (p.dark) .2f else .09f
            val radius = size.minDimension * .85f
            // Animation state is read only during drawing; cards and text do not recompose per frame.
            clipRect {
                fun wash(color: Color, center: Offset, phase: Float, spread: Float) {
                    drawCircle(Brush.radialGradient(listOf(color.copy(alpha = base * strength.value * (.6f + .4f * phase)),
                        Color.Transparent), center, radius * spread), radius * spread, center)
                }
                wash(p.accent, Offset(size.width * .02f, size.height * (.14f + .04f * amber.value)),
                    amber.value, .9f + .18f * amber.value)
                wash(p.warn, Offset(size.width * .98f, size.height * (.55f - .05f * copper.value)),
                    copper.value, .85f + .2f * copper.value)
                wash(p.accentHi, Offset(size.width * .22f, size.height * .97f),
                    gold.value, .8f + .15f * gold.value)
                val ink = p.accent.copy(alpha = (if (p.dark) .055f else .045f) * strength.value)
                ambientGear(Offset(size.width * .96f, size.height * .24f), size.minDimension * .65f, rotation.value, ink)
                ambientGear(Offset(size.width * .04f, size.height * .78f), size.minDimension * .46f, -rotation.value + 15f, ink)
            }
        }
        CompositionLocalProvider(LocalAmbientBackgroundPresent provides true) { content() }
    }
}

private fun DrawScope.ambientGear(center: Offset, edge: Float, angle: Float, ink: Color) {
    inset(center.x - edge / 2, center.y - edge / 2,
        size.width - center.x - edge / 2, size.height - center.y - edge / 2) {
        rotate(angle) { drawGearOutline(ink) }
    }
}
