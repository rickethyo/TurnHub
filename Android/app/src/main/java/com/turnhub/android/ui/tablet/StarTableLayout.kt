package com.turnhub.android.ui.tablet

import android.content.Context
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.size
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import com.turnhub.android.domain.StarCommander
import com.turnhub.android.ui.theme.palette
import kotlin.math.PI
import kotlin.math.cos
import kotlin.math.min
import kotlin.math.roundToInt
import kotlin.math.sin

/**
 * Where seat [index] of a Star Commander table sits, as the setup's star draws
 * it: the top first, then clockwise. [x] and [y] are fractions of the half-width
 * and half-height from the centre; [degrees] turns the panel to face its edge.
 */
internal data class StarSpot(val x: Float, val y: Float, val degrees: Int)

internal fun starSpot(index: Int): StarSpot {
    val a = -PI / 2 + index * 2 * PI / StarCommander.PLAYERS
    // A panel's bottom edge faces the player sitting outside it.
    return StarSpot(cos(a).toFloat(), sin(a).toFloat(), (Math.toDegrees(a) - 90).roundToInt())
}

/** The per-device choice to seat a Star Commander game in its star (setup's shape). */
internal class StarLayoutChoice(val on: Boolean, val set: (Boolean) -> Unit)

@Composable
internal fun rememberStarLayout(): StarLayoutChoice {
    val context = LocalContext.current
    val prefs = remember(context) { context.getSharedPreferences("turnhub_ui", Context.MODE_PRIVATE) }
    var on by remember { mutableStateOf(prefs.getBoolean(STAR_LAYOUT_KEY, false)) }
    return StarLayoutChoice(on) { value ->
        on = value
        prefs.edit().putBoolean(STAR_LAYOUT_KEY, value).apply()
    }
}

private const val STAR_LAYOUT_KEY = "star_table_layout"

internal const val STAR_LAYOUT_HINT = "Seat the game in the star from setup, each panel facing its player. Best on a tablet or large screen."

/**
 * Five panels around the star, each turned toward its player, with the setup's
 * ally (solid) and enemy (dashed) lines faint behind them.
 */
@Composable
internal fun StarTable(panel: @Composable (index: Int, degrees: Int, modifier: Modifier) -> Unit) {
    val p = palette
    BoxWithConstraints(Modifier.fillMaxSize()) {
        val tile: Dp = min(maxWidth.value, maxHeight.value).dp * .34f
        // The star stretches to the screen's shape; tiles stay square and clear of the edges.
        val rx = maxWidth / 2 - tile * .62f
        val ry = maxHeight / 2 - tile * .62f
        Canvas(Modifier.fillMaxSize()) {
            val c = Offset(size.width / 2, size.height / 2)
            fun point(i: Int) = starSpot(i).let { c + Offset(it.x * rx.toPx(), it.y * ry.toPx()) }
            for (a in 0 until StarCommander.PLAYERS) for (b in a + 1 until StarCommander.PLAYERS) {
                val ally = StarCommander.allied(a, b)
                link(point(a), point(b), if (ally) p.accent else p.bad, ally, .35f)
            }
        }
        for (i in 0 until StarCommander.PLAYERS) {
            val spot = starSpot(i)
            Box(Modifier.align(Alignment.Center).offset(rx * spot.x, ry * spot.y).size(tile)) {
                panel(i, spot.degrees, Modifier.fillMaxSize())
            }
        }
    }
}
