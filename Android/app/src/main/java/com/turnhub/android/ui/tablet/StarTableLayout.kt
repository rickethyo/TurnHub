package com.turnhub.android.ui.tablet

import android.content.Context
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.border
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.requiredSize
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.runtime.staticCompositionLocalOf
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.foundation.shape.GenericShape
import androidx.compose.ui.draw.clip
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import com.turnhub.android.domain.StarCommander
import com.turnhub.android.ui.theme.palette
import kotlin.math.PI
import kotlin.math.cos
import kotlin.math.roundToInt
import kotlin.math.sin

/**
 * Where seat [index] of a Star Commander table sits, as the setup's star draws
 * it: the top first, then clockwise. [x] and [y] are fractions of the half-width
 * and half-height from the centre; [degrees] turns the panel to face its edge.
 */
internal data class StarSpot(val x: Float, val y: Float, val degrees: Int)
internal val LocalStarSeatFlow = staticCompositionLocalOf<StarSeatFlow?> { null }

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
 * Five fitted seat areas, with artwork filling each wedge and controls safely
 * inside it. The relationship lines show only through the gutters.
 */
@Composable
internal fun StarTable(panel: @Composable (index: Int, degrees: Int, modifier: Modifier) -> Unit,
    active: (Int) -> Boolean, backdrop: @Composable (Int, Modifier) -> Unit) {
    val p = palette
    val density = LocalDensity.current.density
    BoxWithConstraints(Modifier.fillMaxSize()) {
        val viewportWidth = maxWidth
        val viewportHeight = maxHeight
        val cards = remember(maxWidth, maxHeight) { starTableGeometry(maxWidth.value, maxHeight.value) }
        Canvas(Modifier.fillMaxSize()) {
            fun point(i: Int) = cards[i].let { Offset(it.x.dp.toPx(), it.y.dp.toPx()) }
            for (a in 0 until StarCommander.PLAYERS) for (b in a + 1 until StarCommander.PLAYERS) {
                val ally = StarCommander.allied(a, b)
                link(point(a), point(b), if (ally) p.accent else p.bad, ally, .22f)
            }
        }
        cards.forEachIndexed { i, card ->
            val shape = remember(card, density) { GenericShape { _, _ ->
                val points = card.seat
                for (j in points.indices) {
                    val a = points[(j + points.size - 1) % points.size]
                    val b = points[j]
                    val c = points[(j + 1) % points.size]
                    fun near(other: StarPoint): StarPoint {
                        val dx = other.x - b.x; val dy = other.y - b.y
                        val fraction = minOf(.25f, 9f / kotlin.math.sqrt(dx * dx + dy * dy))
                        return StarPoint(b.x + dx * fraction, b.y + dy * fraction)
                    }
                    val entry = near(a); val exit = near(c)
                    if (j == 0) moveTo(entry.x * density, entry.y * density) else lineTo(entry.x * density, entry.y * density)
                    quadraticTo(b.x * density, b.y * density, exit.x * density, exit.y * density)
                }
                close()
            } }
            Box(Modifier.fillMaxSize().clip(shape).background(p.surface)) {
                val art = card.backdrop!!
                Box(Modifier.align(Alignment.Center).offset(art.x.dp - viewportWidth / 2, art.y.dp - viewportHeight / 2)
                    .requiredSize(art.width.dp, art.height.dp).graphicsLayer { rotationZ = art.degrees.toFloat() }) {
                    backdrop(i, Modifier.fillMaxSize())
                }
            }
            Box(Modifier.fillMaxSize().border(if (active(i)) 3.dp else 1.dp,
                if (active(i)) p.active else p.line, shape))
        }
        for (i in 0 until StarCommander.PLAYERS) {
            val card = cards[i]
            val frame = card.backdrop!!
            val fontScale = LocalDensity.current.fontScale
            val flow = remember(card, fontScale) { starSeatFlow(card, fontScale) }
            Box(Modifier.align(Alignment.Center).offset(frame.x.dp - maxWidth / 2, frame.y.dp - maxHeight / 2)
                .requiredSize(frame.width.dp, frame.height.dp).graphicsLayer { rotationZ = card.degrees.toFloat() }) {
                androidx.compose.runtime.CompositionLocalProvider(LocalStarSeatFlow provides flow) {
                    panel(i, 0, Modifier.fillMaxSize())
                }
            }
        }
    }
}

@Composable
internal fun StarSeatArtwork(unit: TableUnit, modifier: Modifier) {
    val member = unit.members.first()
    val art = rememberCommanderArt(member.commander, member.commanderArt)
    val p = if (!palette.dark && (art != null || member.tileBackground.style == com.turnhub.android.domain.TileBackgroundStyle.IMAGE && member.tileBackground.image != null))
        com.turnhub.android.ui.theme.TurnHubPalette.Graphite else palette
    androidx.compose.runtime.CompositionLocalProvider(com.turnhub.android.ui.theme.LocalTurnHubPalette provides p) {
        PlayerTileBackground(member.tileBackground,
            com.turnhub.android.ui.components.playerColor(member.color, p.avatarColor(member.playerNumber)),
            modifier, art, showCredit = false)
    }
}
