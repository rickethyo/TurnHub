package com.turnhub.android.ui.tablet

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.gestures.detectDragGestures
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.setValue
import androidx.compose.runtime.mutableStateMapOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Rect
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.layout.boundsInRoot
import androidx.compose.ui.layout.onGloballyPositioned
import androidx.compose.ui.platform.LocalConfiguration
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
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.zIndex
import com.turnhub.android.ui.theme.palette
import com.turnhub.android.domain.TileBackground
import androidx.compose.ui.graphics.Color

/**
 * A small copy of the full-screen table for the lobby: each player's panel
 * where it will land, turned the way it will face, numbered in turn order.
 * Drag a panel onto another to swap the two, or tap one and then the other
 * (also the screen-reader path, with a "Swap with" action per seat).
 * With [teams] (Two-Headed Giant) each panel is a team split in two:
 * players 1 and 2, then 3 and 4, with empty halves until the team fills.
 * Each panel shows its player's tile card art ([cards]: name and printing), as the table will.
 */
@Composable
internal fun SeatMap(names: List<String>, onSwap: (Int, Int) -> Unit, modifier: Modifier = Modifier,
    backgrounds: List<TileBackground> = emptyList(), colors: List<String?> = emptyList(), teams: Boolean = false,
    cards: List<Pair<String?, String?>> = emptyList()) {
    val p = palette
    val swap by rememberUpdatedState(onSwap)
    val configuration = LocalConfiguration.current
    // Panels: one per player, or one per team of two. Reserve both ends even before the
    // second panel fills, so setup doesn't jump.
    val panels = (if (teams) (names.size + 1) / 2 else names.size).coerceAtLeast(2)
    // The same choice the table makes: side-by-side rows unless the screen is upright and more than two panels.
    val landscape = configuration.screenWidthDp >= configuration.screenHeightDp || panels <= 2
    val plan = placements(panels)
    val bounds = remember(names.size) { mutableStateMapOf<Int, Rect>() }
    var picked by remember(names.size) { mutableStateOf<Int?>(null) }
    var dragging by remember(names.size) { mutableStateOf<Int?>(null) }
    var dragOffset by remember(names.size) { mutableStateOf(Offset.Zero) }
    var pointer by remember(names.size) { mutableStateOf(Offset.Zero) }
    val target = dragging?.let { from -> bounds.entries.firstOrNull { it.key != from && it.key < names.size && it.value.contains(pointer) }?.key }
    if (picked != null && picked!! >= names.size) picked = null

    val seat: @Composable (Int, Int, Modifier) -> Unit = seat@ { index, degrees, slotModifier ->
        if (index !in names.indices) {
            Box(
                slotModifier.padding(3.dp).clip(RoundedCornerShape(12.dp))
                    .background(p.surface).border(1.dp, p.line, RoundedCornerShape(12.dp))
                    .clearAndSetSemantics { contentDescription = "Seat ${index + 1}, empty" },
                contentAlignment = Alignment.Center,
            ) {
                Text("Empty seat", color = p.muted, style = MaterialTheme.typography.labelLarge,
                    modifier = Modifier.padding(4.dp), textAlign = TextAlign.Center)
            }
            return@seat
        }
        val hue = colors.getOrNull(index)?.let { Color(android.graphics.Color.parseColor(it)) } ?: p.avatarColor(index + 1)
        val shape = RoundedCornerShape(12.dp)
        val isDragged = dragging == index
        val highlighted = picked == index || target == index
        val others = names.indices.filter { it != index }
        Box(
            slotModifier
                .padding(3.dp)
                .zIndex(if (isDragged) 1f else 0f)
                .onGloballyPositioned { bounds[index] = it.boundsInRoot() }
                .graphicsLayer {
                    if (isDragged) {
                        translationX = dragOffset.x
                        translationY = dragOffset.y
                        scaleX = 1.06f
                        scaleY = 1.06f
                        shadowElevation = 12f
                    }
                }
                .clip(shape)
                .background(p.surface)
                .border(if (highlighted) 3.dp else 1.dp, if (highlighted) p.active else p.line, shape)
                .pointerInput(index, names.size) {
                    detectTapGestures {
                        val first = picked
                        picked = when (first) {
                            null -> index
                            index -> null
                            else -> { swap(first, index); null }
                        }
                    }
                }
                .pointerInput(index, names.size) {
                    detectDragGestures(
                        onDragStart = { at ->
                            picked = null
                            dragging = index
                            dragOffset = Offset.Zero
                            pointer = (bounds[index]?.topLeft ?: Offset.Zero) + at
                        },
                        onDrag = { change, amount ->
                            change.consume()
                            dragOffset += amount
                            pointer += amount
                        },
                        onDragEnd = {
                            val from = dragging
                            val to = from?.let { f -> bounds.entries.firstOrNull { it.key != f && it.key < names.size && it.value.contains(pointer) }?.key }
                            if (from != null && to != null) swap(from, to)
                            dragging = null
                            dragOffset = Offset.Zero
                        },
                        onDragCancel = {
                            dragging = null
                            dragOffset = Offset.Zero
                        },
                    )
                }
                .clearAndSetSemantics {
                    contentDescription = "Seat ${index + 1}, ${names[index]}" +
                        if (picked == index) ", picked. Tap another seat to swap" else ""
                    role = Role.Button
                    selected = picked == index
                    onClick(label = "Pick to swap") {
                        val first = picked
                        picked = when (first) { null -> index; index -> null; else -> { swap(first, index); null } }
                        true
                    }
                    customActions = others.map { other ->
                        CustomAccessibilityAction("Swap with ${names[other]}") { swap(index, other); true }
                    }
                },
            contentAlignment = Alignment.Center,
        ) {
            PlayerTileBackground(backgrounds.getOrNull(index) ?: TileBackground(), hue, Modifier.fillMaxSize().facing(degrees),
                art = cards.getOrNull(index)?.let { (card, printing) -> rememberCommanderArt(card, printing) }, showCredit = false)
            Box(Modifier.fillMaxSize().facing(degrees), contentAlignment = Alignment.Center) {
                Column(horizontalAlignment = Alignment.CenterHorizontally, modifier = Modifier.padding(4.dp)) {
                    Box(Modifier.size(24.dp).background(hue, CircleShape), contentAlignment = Alignment.Center) {
                        Text("${index + 1}", color = p.avatarText, style = MaterialTheme.typography.labelMedium, fontWeight = FontWeight.Bold)
                    }
                    Text(names[index], color = p.text, style = MaterialTheme.typography.labelLarge, fontWeight = FontWeight.SemiBold,
                        maxLines = 1, overflow = TextOverflow.Ellipsis, textAlign = TextAlign.Center)
                }
            }
        }
    }

    // A team's two halves, ordered left to right as its players see them once turned.
    val team: @Composable (Int, Int, Modifier) -> Unit = { unit, degrees, slotModifier ->
        val members = listOf(2 * unit, 2 * unit + 1).let { if (degrees == 180 || degrees == 270) it.reversed() else it }
        val shape = RoundedCornerShape(14.dp)
        val frame = slotModifier.padding(2.dp).clip(shape).border(2.dp, p.accent, shape).padding(2.dp)
        if (degrees % 180 == 0) Row(frame) { members.forEach { seat(it, degrees, Modifier.weight(1f).fillMaxHeight()) } }
        else Column(frame) { members.forEach { seat(it, degrees, Modifier.weight(1f).fillMaxWidth()) } }
    }
    val slot: @Composable (Int, Int, Modifier) -> Unit = if (teams) team else seat

    Box(modifier.fillMaxWidth(), contentAlignment = Alignment.Center) { Box(
        Modifier
            .wrapAspect(landscape)
            .clip(RoundedCornerShape(18.dp))
            .background(p.bg)
            .border(1.dp, p.lineStrong, RoundedCornerShape(18.dp))
            .padding(4.dp),
    ) {
        if (landscape) {
            Column(Modifier.fillMaxSize()) {
                if (plan.far.isNotEmpty()) {
                    Row(Modifier.weight(1f).fillMaxWidth()) { plan.far.forEach { slot(it, 180, Modifier.weight(1f).fillMaxHeight()) } }
                }
                Row(Modifier.weight(1f).fillMaxWidth()) { plan.near.forEach { slot(it, 0, Modifier.weight(1f).fillMaxHeight()) } }
            }
        } else {
            Row(Modifier.fillMaxSize()) {
                if (plan.far.isNotEmpty()) {
                    Column(Modifier.weight(1f).fillMaxHeight()) { plan.far.asReversed().forEach { slot(it, 90, Modifier.weight(1f).fillMaxWidth()) } }
                }
                Column(Modifier.weight(1f).fillMaxHeight()) { plan.near.asReversed().forEach { slot(it, 270, Modifier.weight(1f).fillMaxWidth()) } }
            }
        }
        // The centre menu disc, so the miniature reads as the same table.
        Box(Modifier.align(Alignment.Center).size(26.dp).background(p.surface, CircleShape).border(1.dp, p.lineStrong, CircleShape))
    } }
}

/** Wide like the table on its side; upright and capped so a phone's portrait map isn't a scroll of its own. */
private fun Modifier.wrapAspect(landscape: Boolean): Modifier =
    if (landscape) fillMaxWidth().aspectRatio(16f / 10f)
    else height(420.dp).aspectRatio(10f / 15f, matchHeightConstraintsFirst = true)
