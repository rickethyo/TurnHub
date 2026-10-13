package com.turnhub.android.ui.tablet

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.PathEffect
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.semantics.CustomAccessibilityAction
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.clearAndSetSemantics
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.customActions
import androidx.compose.ui.semantics.onClick
import androidx.compose.ui.semantics.role
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextDecoration
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import com.turnhub.android.domain.StarCommander
import com.turnhub.android.domain.TableSummary
import com.turnhub.android.protocol.AvatarIcon
import com.turnhub.android.ui.components.PlayerAvatar
import com.turnhub.android.ui.theme.palette
import kotlin.math.PI
import kotlin.math.cos
import kotlin.math.min
import kotlin.math.sin

/** One Star Commander seat as the star map draws it; index in the list is the seat (turn order). */
internal data class StarSeat(val name: String, val playerNumber: Int, val avatar: AvatarIcon? = null, val eliminated: Boolean = false)

internal fun TableSummary.starSeats(): List<StarSeat> =
    players.sortedBy { it.playerNumber }.map { StarSeat(it.label, it.playerNumber, it.avatar, it.eliminated) }

/** "Allies: B, E · Enemies: C, D" for [seat], the same words the map's labels and legend use. */
internal fun starRelations(seats: List<StarSeat>, seat: Int): String {
    fun names(of: List<Int>) = of.joinToString(", ") { s -> seats[s].name + if (seats[s].eliminated) " (out)" else "" }
    return "Allies: ${names(StarCommander.allies(seat))} · Enemies: ${names(StarCommander.enemies(seat))}"
}

/** "Enemies out 1/2", the progress toward a Star Commander win. */
internal fun starProgress(seats: List<StarSeat>, seat: Int): String =
    "Enemies out ${StarCommander.enemiesOut(seat, seats.map { it.eliminated })}/2"

/**
 * Star Commander's five seats on a pentagon, in seat (turn) order clockwise
 * from the top. Allies sit on the pentagon's edges, drawn solid; enemies on the
 * star's diagonals, drawn dashed, and the legend names both, so the colours are
 * never the only cue. Tapping a player highlights their allies and enemies and
 * says how many enemies are out. With [onSwap] (the lobby) a tap picks a player
 * and a tap on another swaps their seats, as on [SeatMap]; screen readers get a
 * "Swap with" action per seat. Nothing animates, so reduced motion needs no case.
 */
@Composable
internal fun StarMap(seats: List<StarSeat>, modifier: Modifier = Modifier, onSwap: ((Int, Int) -> Unit)? = null) {
    if (seats.size != StarCommander.PLAYERS) return
    val p = palette
    val swap by rememberUpdatedState(onSwap)
    var focus by remember { mutableStateOf<Int?>(null) }
    val allyColor = p.accent
    val enemyColor = p.bad
    fun tap(index: Int) {
        val first = focus
        val swapper = swap
        focus = when {
            first == null -> index
            first == index -> null
            swapper != null -> { swapper(first, index); null }
            else -> index
        }
    }

    Column(modifier.fillMaxWidth(), verticalArrangement = Arrangement.spacedBy(8.dp), horizontalAlignment = Alignment.CenterHorizontally) {
        BoxWithConstraints(
            Modifier
                .fillMaxWidth()
                .clip(RoundedCornerShape(18.dp))
                .background(p.bg)
                .border(1.dp, p.lineStrong, RoundedCornerShape(18.dp)),
            contentAlignment = Alignment.Center,
        ) {
            val side = min(maxWidth.value, 360f).dp
            val node = 88.dp
            val radius = side / 2 - node / 2
            // Seat i sits at -90° + 72°·i: the top first, then clockwise.
            fun at(i: Int): Pair<Float, Float> {
                val a = -PI / 2 + i * 2 * PI / StarCommander.PLAYERS
                return (cos(a) * radius.value).toFloat() to (sin(a) * radius.value).toFloat()
            }
            Box(Modifier.size(side), contentAlignment = Alignment.Center) {
                Canvas(Modifier.fillMaxSize()) {
                    val c = Offset(size.width / 2, size.height / 2)
                    fun point(i: Int) = at(i).let { (x, y) -> c + Offset(x.dp.toPx(), y.dp.toPx()) }
                    for (a in 0 until StarCommander.PLAYERS) for (b in a + 1 until StarCommander.PLAYERS) {
                        val involved = focus == null || focus == a || focus == b
                        val ally = StarCommander.allied(a, b)
                        link(point(a), point(b), if (ally) allyColor else enemyColor, ally, if (involved) 1f else .18f)
                    }
                }
                seats.forEachIndexed { i, seat ->
                    val (x, y) = at(i)
                    val relation = focus?.let { f ->
                        when {
                            f == i -> null
                            StarCommander.allied(f, i) -> "Ally"
                            else -> "Enemy"
                        }
                    }
                    val selected = focus == i
                    Column(
                        Modifier
                            .offset(x.dp, y.dp)
                            .width(node)
                            .clip(RoundedCornerShape(12.dp))
                            .background(if (selected) p.activeSoft else p.bg.copy(alpha = .85f))
                            .border(if (selected) 2.dp else 0.dp, if (selected) p.active else Color.Transparent, RoundedCornerShape(12.dp))
                            .pointerInput(i) { detectTapGestures { tap(i) } }
                            .clearAndSetSemantics {
                                contentDescription = "Seat ${i + 1}, ${seat.name}" + (if (seat.eliminated) ", out" else "") +
                                    ". ${starRelations(seats, i)}. ${starProgress(seats, i)}" +
                                    (if (selected && swap != null) ". Picked: tap another player to swap seats" else "")
                                role = Role.Button
                                this.selected = selected
                                onClick(label = if (swap != null) "Pick to swap" else "Show allies and enemies") { tap(i); true }
                                if (swap != null) {
                                    customActions = seats.indices.filter { it != i }.map { other ->
                                        CustomAccessibilityAction("Swap with ${seats[other].name}") { swap?.invoke(i, other); true }
                                    }
                                }
                            }
                            .padding(4.dp),
                        horizontalAlignment = Alignment.CenterHorizontally,
                    ) {
                        Box(Modifier.alpha(if (seat.eliminated) .45f else 1f)) {
                            PlayerAvatar(seat.name, seat.playerNumber, seat.avatar, size = 40.dp)
                        }
                        Text(
                            seat.name, color = p.text, style = MaterialTheme.typography.labelLarge, fontWeight = FontWeight.SemiBold,
                            maxLines = 1, overflow = TextOverflow.Ellipsis, textAlign = TextAlign.Center,
                            textDecoration = if (seat.eliminated) TextDecoration.LineThrough else null,
                        )
                        Text(
                            listOfNotNull("Seat ${i + 1}", relation, "out".takeIf { seat.eliminated }).joinToString(" · "),
                            color = when (relation) { "Ally" -> p.text; "Enemy" -> p.bad; else -> p.muted },
                            style = MaterialTheme.typography.labelSmall, maxLines = 1,
                        )
                    }
                }
            }
        }
        // The legend: line style and words, never colour alone.
        Row(horizontalArrangement = Arrangement.spacedBy(16.dp), verticalAlignment = Alignment.CenterVertically) {
            LegendLine(allyColor, solid = true)
            Text("Allies (protected)", color = p.text, style = MaterialTheme.typography.labelMedium)
            LegendLine(enemyColor, solid = false)
            Text("Enemies", color = p.text, style = MaterialTheme.typography.labelMedium)
        }
        focus?.let { f ->
            Text("${seats[f].name}: ${starRelations(seats, f)} · ${starProgress(seats, f)}",
                color = p.text, style = MaterialTheme.typography.bodySmall, textAlign = TextAlign.Center)
        }
    }
}

/** One relationship line: allies solid and thicker, enemies dashed. */
private fun DrawScope.link(from: Offset, to: Offset, color: Color, ally: Boolean, alpha: Float) {
    drawLine(
        color.copy(alpha = alpha),
        from,
        to,
        strokeWidth = (if (ally) 4.dp else 2.5.dp).toPx(),
        pathEffect = if (ally) null else PathEffect.dashPathEffect(floatArrayOf(10.dp.toPx(), 7.dp.toPx())),
    )
}

@Composable
private fun LegendLine(color: Color, solid: Boolean) {
    Canvas(Modifier.size(width = 32.dp, height = 10.dp)) {
        link(Offset(0f, size.height / 2), Offset(size.width, size.height / 2), color, solid, 1f)
    }
}

/** Every player's allies and enemies as text, for the lobby (and screen readers). */
@Composable
internal fun StarRelationsList(seats: List<StarSeat>) {
    if (seats.size != StarCommander.PLAYERS) return
    val p = palette
    Column(verticalArrangement = Arrangement.spacedBy(2.dp)) {
        seats.forEachIndexed { i, seat ->
            Text("${i + 1}. ${seat.name} · ${starRelations(seats, i)}", color = p.muted, style = MaterialTheme.typography.bodySmall)
        }
    }
}

/** How the star map is explained in both lobbies. */
internal const val STAR_MAP_HINT = "Your neighbours are your allies: they can't deal you Commander damage. " +
    "The two players across the star are your enemies; win when both are out, and allies can share the win. " +
    "Tap a player to see their allies and enemies, then another to swap their seats."
