package com.turnhub.android.ui.tablet

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.gestures.waitForUpOrCancellation
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.RowScope
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.key
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.mutableStateMapOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.layout.layout
import androidx.compose.ui.semantics.LiveRegionMode
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.clearAndSetSemantics
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.disabled
import androidx.compose.ui.semantics.liveRegion
import androidx.compose.ui.semantics.onClick
import androidx.compose.ui.semantics.role
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Constraints
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.turnhub.android.data.TabletSeat
import com.turnhub.android.data.TabletState
import com.turnhub.android.domain.TableClock
import com.turnhub.android.domain.TablePlayer
import com.turnhub.android.domain.TableSummary
import com.turnhub.android.protocol.GameProfile
import com.turnhub.android.protocol.LifeRequestState
import com.turnhub.android.protocol.TableState
import com.turnhub.android.protocol.TurnTimerPhase
import com.turnhub.android.ui.components.AccentButton
import com.turnhub.android.ui.components.BrassCard
import com.turnhub.android.ui.components.Tone
import com.turnhub.android.ui.components.ToneButton
import com.turnhub.android.ui.components.TurnHubHaptics
import com.turnhub.android.ui.components.rememberHaptics
import com.turnhub.android.ui.components.rememberNowMs
import com.turnhub.android.ui.theme.palette
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch

/** One panel: a player, or a Two-Headed Giant team sharing one life total. */
internal data class TableUnit(val key: String, val members: List<TablePlayer>) {
    /** Life lives on the first player still in (a team's members carry the same total). */
    val lifeSeat: TablePlayer get() = members.firstOrNull { !it.eliminated } ?: members.first()
    val out: Boolean get() = members.all { it.eliminated }
    val name: String get() = members.joinToString(" & ") { it.label }
}

internal fun tableUnits(summary: TableSummary): List<TableUnit> =
    if (!summary.settings.twoHeadedGiant) {
        summary.players.map { TableUnit("p${it.playerNumber}", listOf(it)) }
    } else {
        summary.players.groupBy { it.team ?: -it.playerNumber }.map { (team, members) -> TableUnit("t$team", members) }
    }

/**
 * Where each panel sits, as the portal: the near side holds ceil(n/2) panels
 * and the far side the rest. Seats run clockwise from the near-left corner:
 * up the left end, along the far side, back along the near side. [near] and
 * [far] hold unit indices in left-to-right order (seen from the near side).
 */
internal data class Placement(val near: List<Int>, val far: List<Int>)

internal fun placements(count: Int): Placement {
    if (count <= 0) return Placement(emptyList(), emptyList())
    val nearCount = (count + 1) / 2
    val farCount = count / 2
    val near = IntArray(nearCount)
    val far = IntArray(farCount)
    var index = 0
    near[0] = index++
    for (col in 0 until farCount) far[col] = index++
    for (col in nearCount - 1 downTo 1) near[col] = index++
    return Placement(near.toList(), far.toList())
}

/** Life steps per tap and per held repeat: Yu-Gi-Oh! counts in hundreds. */
internal fun lifeSteps(profile: GameProfile): Pair<Int, Int> =
    if (profile == GameProfile.YUGIOH) 100 to 1000 else 1 to 5

/**
 * Lays the content out for a panel turned by [degrees] (a multiple of 90) and
 * draws it turned, so a player on any side reads it upright. Touches follow
 * the turn.
 */
internal fun Modifier.facing(degrees: Int): Modifier = layout { measurable, constraints ->
    val w = constraints.maxWidth
    val h = constraints.maxHeight
    val sideways = degrees % 180 != 0
    val placeable = measurable.measure(if (sideways) Constraints.fixed(h, w) else Constraints.fixed(w, h))
    layout(w, h) {
        placeable.placeWithLayer((w - placeable.width) / 2, (h - placeable.height) / 2) {
            rotationZ = degrees.toFloat()
        }
    }
}

/** A life change sent to Atlas and shown until the snapshot carries it. */
private data class SentLife(val id: Long, val key: String, val delta: Int, val sentAtRevision: Long, val acked: Boolean = false)

private const val GATHER_MS = 900L
private const val HOLD_MS = 450L
private const val REPEAT_MS = 450L
private const val PASS_HOLD_MS = 600L

@Composable
internal fun TabletTable(
    summary: TableSummary,
    tablet: TabletState,
    actions: TabletActions,
    reduceMotion: Boolean,
    onClose: () -> Unit,
) {
    val p = palette
    val haptics = rememberHaptics()
    val scope = rememberCoroutineScope()
    val current by rememberUpdatedState(summary)
    // Life taps gather per panel and go to Atlas once after a short pause.
    val unsent = remember { mutableStateMapOf<String, Int>() }
    val sent = remember { mutableStateListOf<SentLife>() }
    val gathering = remember { mutableMapOf<String, Job>() }
    var nextId by remember { mutableStateOf(0L) }
    val drawers = remember { mutableStateListOf<String>() }
    var flipped by rememberSaveable { mutableStateOf(false) }
    var menuOpen by remember { mutableStateOf(false) }
    val nowMs = rememberNowMs(ticking = summary.state == TableState.RUNNING, intervalMs = 500)

    LaunchedEffect(summary.revision) { sent.removeAll { it.acked && it.sentAtRevision != summary.revision } }
    LaunchedEffect(tablet.message) {
        if (tablet.message != null) {
            delay(3_500)
            actions.onDismissMessage()
        }
    }

    fun send(seat: TabletSeat?, action: String) {
        seat ?: return
        haptics.tick()
        actions.run { this.control(seat, action) }
    }

    fun flush(key: String) {
        val delta = unsent.remove(key) ?: return
        if (delta == 0) return
        val unit = tableUnits(current).firstOrNull { it.key == key } ?: return
        val entry = SentLife(nextId++, key, delta, current.revision)
        sent.add(entry)
        actions.run {
            if (this.life(unit.lifeSeat.seat(), delta)) {
                val i = sent.indexOfFirst { it.id == entry.id }
                if (i >= 0) sent[i] = entry.copy(acked = true)
                // Normally the next snapshot replaces it; this covers one that already carried it.
                scope.launch {
                    delay(2_000)
                    sent.removeAll { it.id == entry.id }
                }
            } else {
                sent.removeAll { it.id == entry.id }
            }
        }
    }

    fun bump(unit: TableUnit, delta: Int) {
        unsent[unit.key] = (unsent[unit.key] ?: 0) + delta
        haptics.tick()
        gathering[unit.key]?.cancel()
        gathering[unit.key] = scope.launch {
            delay(GATHER_MS)
            flush(unit.key)
        }
    }

    val units = tableUnits(summary)
    val turn = if (flipped) 180 else 0
    BoxWithConstraints(Modifier.fillMaxSize().background(p.bg)) {
        val landscape = maxWidth >= maxHeight || units.size <= 2
        val plan = placements(units.size)
        val panel: @Composable (Int, Int, Modifier) -> Unit = { index, degrees, modifier ->
            val unit = units[index]
            key(unit.key) {
                val pending = unsent[unit.key] ?: 0
                val life = (unit.lifeSeat.life ?: 0) + pending + sent.filter { it.key == unit.key }.sumOf { it.delta }
                Box(modifier.padding(4.dp).facing((degrees + turn) % 360)) {
                    PlayerPanel(
                        unit = unit,
                        summary = summary,
                        life = life,
                        pendingDelta = pending,
                        nowMs = nowMs,
                        drawerOpen = unit.key in drawers,
                        onToggleDrawer = { if (unit.key in drawers) drawers.remove(unit.key) else drawers.add(unit.key) },
                        onBump = { bump(unit, it) },
                        onControl = { seat, action ->
                            if (action == "concede") drawers.remove(unit.key)
                            send(seat, action)
                        },
                        onCommander = { seat, source, which, delta ->
                            haptics.tick()
                            actions.run { this.commander(seat, source, which, delta) }
                        },
                        onRespond = { seat, id, accept -> actions.run { this.respondLife(seat, id, accept) } },
                        haptics = haptics,
                    )
                }
            }
        }
        if (landscape) {
            Column(Modifier.fillMaxSize().padding(4.dp)) {
                if (plan.far.isNotEmpty()) {
                    Row(Modifier.weight(1f).fillMaxWidth()) {
                        plan.far.forEach { panel(it, 180, Modifier.weight(1f).fillMaxHeight()) }
                    }
                }
                Row(Modifier.weight(1f).fillMaxWidth()) {
                    plan.near.forEach { panel(it, 0, Modifier.weight(1f).fillMaxHeight()) }
                }
            }
        } else {
            // Portrait: the long sides become columns; each fills from the bottom
            // so turn order stays clockwise.
            Row(Modifier.fillMaxSize().padding(4.dp)) {
                if (plan.far.isNotEmpty()) {
                    Column(Modifier.weight(1f).fillMaxHeight()) {
                        plan.far.asReversed().forEach { panel(it, 90, Modifier.weight(1f).fillMaxWidth()) }
                    }
                }
                Column(Modifier.weight(1f).fillMaxHeight()) {
                    plan.near.asReversed().forEach { panel(it, 270, Modifier.weight(1f).fillMaxWidth()) }
                }
            }
        }

        Hub(summary, nowMs, Modifier.align(Alignment.Center)) {
            haptics.tick()
            menuOpen = true
        }

        tablet.message?.let { message ->
            Text(
                message.message,
                color = p.text,
                style = MaterialTheme.typography.bodyMedium,
                textAlign = TextAlign.Center,
                modifier = Modifier
                    .align(Alignment.Center)
                    .padding(top = 140.dp)
                    .widthIn(max = 420.dp)
                    .background(p.surface, RoundedCornerShape(12.dp))
                    .border(1.dp, if (message.isError) p.bad else p.line, RoundedCornerShape(12.dp))
                    .padding(horizontal = 16.dp, vertical = 10.dp)
                    .semantics { liveRegion = LiveRegionMode.Polite },
            )
        }

        val anySeat = summary.anySeat()
        val activeSeat = summary.players.firstOrNull { it.playerNumber == summary.activePlayerNumber }?.seat() ?: anySeat
        when (summary.state) {
            TableState.STARTING -> Overlay("Starting…", "The game begins in a moment.") {
                ToneButton("Cancel", { send(anySeat, "cancel-start") }, Modifier.fillMaxWidth())
            }
            TableState.PAUSED -> Overlay("Paused", "The clocks are stopped.") {
                AccentButton("Resume", { send(activeSeat, "pause") }, Modifier.fillMaxWidth())
            }
            TableState.GAME_OVER -> {
                val winner = summary.players.firstOrNull { it.playerNumber == summary.winnerPlayerNumber }
                val winners = if (winner == null) null else units.firstOrNull { u -> u.members.any { it.playerNumber == winner.playerNumber } }
                Overlay(
                    if (winners != null) "${winners.name} ${if (winners.members.size > 1) "win" else "wins"}" else "Game over",
                    if (winners != null) "Well played, everyone." else "The game ended as a draw.",
                ) {
                    OverlayButtons {
                        ToneButton("New players", { send(anySeat, "reset") }, Modifier.weight(1f))
                        AccentButton("Rematch", { send(anySeat, "rematch") }, Modifier.weight(1f))
                    }
                }
            }
            else -> if (menuOpen) {
                Overlay("Table", "Game clock ${TableClock.format(TableClock.gameElapsedMs(summary, nowMs))}", onDismiss = { menuOpen = false }) {
                    ToneButton("Pause", { menuOpen = false; send(activeSeat, "pause") }, Modifier.fillMaxWidth())
                    ToneButton("Turn panels around", { flipped = !flipped; menuOpen = false }, Modifier.fillMaxWidth())
                    ToneButton("Back to the app", { menuOpen = false; onClose() }, Modifier.fillMaxWidth())
                    ToneButton("Leave tablet mode", {
                        menuOpen = false
                        actions.run { this.disable() }
                        onClose()
                    }, Modifier.fillMaxWidth(), tone = Tone.BAD)
                    AccentButton("Close", { menuOpen = false }, Modifier.fillMaxWidth())
                }
            }
        }
    }
}

/** The centre disc: the game clock, and the table menu on tap. */
@Composable
private fun Hub(summary: TableSummary, nowMs: Long, modifier: Modifier, onOpen: () -> Unit) {
    val p = palette
    val clock = TableClock.format(TableClock.gameElapsedMs(summary, nowMs))
    Column(
        modifier
            .size(84.dp)
            .clip(CircleShape)
            .background(p.surface)
            .border(2.dp, p.lineStrong, CircleShape)
            .clickable(onClickLabel = "Open the table menu", role = Role.Button, onClick = onOpen),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.Center,
    ) {
        Text(clock, color = p.text, style = MaterialTheme.typography.titleMedium, fontWeight = FontWeight.SemiBold)
        Text("Menu", color = p.muted, style = MaterialTheme.typography.labelSmall)
    }
}

/** A card over the whole table for its states (starting, paused, game over) and the menu. */
@Composable
private fun Overlay(
    title: String,
    text: String,
    onDismiss: (() -> Unit)? = null,
    content: @Composable () -> Unit,
) = OverlayFrame(title, text, onDismiss, content)

@Composable
private fun OverlayButtons(content: @Composable RowScope.() -> Unit) =
    Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(10.dp), content = content)

@Composable
private fun OverlayFrame(title: String, text: String, onDismiss: (() -> Unit)?, content: @Composable () -> Unit) {
    val p = palette
    Box(
        Modifier
            .fillMaxSize()
            .background(p.scrim)
            // Swallows touches so nothing under the card changes; a tap outside closes the menu.
            .pointerInput(onDismiss) { detectTapGestures { onDismiss?.invoke() } },
        contentAlignment = Alignment.Center,
    ) {
        BrassCard(Modifier.widthIn(max = 380.dp).padding(16.dp).pointerInput(Unit) { detectTapGestures { } }) {
            Text(title, color = p.text, style = MaterialTheme.typography.headlineSmall)
            Text(text, color = p.muted)
            Column(verticalArrangement = Arrangement.spacedBy(8.dp)) { content() }
        }
    }
}

// --- one player's panel -----------------------------------------------------------

@Composable
private fun PlayerPanel(
    unit: TableUnit,
    summary: TableSummary,
    life: Int,
    pendingDelta: Int,
    nowMs: Long,
    drawerOpen: Boolean,
    onToggleDrawer: () -> Unit,
    onBump: (Int) -> Unit,
    onControl: (TabletSeat, String) -> Unit,
    onCommander: (TabletSeat, Int, Int, Int) -> Unit,
    onRespond: (TabletSeat, Long, Boolean) -> Unit,
    haptics: TurnHubHaptics,
) {
    val p = palette
    val shape = RoundedCornerShape(22.dp)
    val hue = p.avatarColor(unit.members.first().playerNumber)
    val active = unit.members.any { summary.hasTurn(it.playerNumber) } && summary.state != TableState.GAME_OVER
    val out = unit.out
    val (small, big) = lifeSteps(summary.settings.profile)
    val name = unit.name
    BoxWithConstraints(
        Modifier
            .fillMaxSize()
            .clip(shape)
            .background(p.surface)
            .background(hue.copy(alpha = if (p.dark) .22f else .16f))
            .border(if (active) 3.dp else 1.dp, if (active) p.active else p.line, shape),
    ) {
        val nameMax = maxWidth * .6f
        val lifeSize = (minOf(maxWidth, maxHeight).value * if ("$life".length > 3) .22f else .36f).coerceIn(28f, 160f)
        Column(Modifier.fillMaxSize().alpha(if (out) .45f else 1f)) {
            HoldHalf("+", "$name: add $small life", !out, { onBump(small) }, { onBump(big) }, Alignment.TopEnd, Modifier.weight(1f).fillMaxWidth())
            HoldHalf("−", "$name: subtract $small life", !out, { onBump(-small) }, { onBump(-big) }, Alignment.BottomEnd, Modifier.weight(1f).fillMaxWidth())
        }

        Row(
            Modifier.align(Alignment.TopStart).padding(horizontal = 16.dp, vertical = 12.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            Text(name, color = p.text, style = MaterialTheme.typography.titleMedium, fontWeight = FontWeight.SemiBold,
                maxLines = 1, overflow = TextOverflow.Ellipsis, modifier = Modifier.widthIn(max = nameMax))
            if (active) Tag("Turn", p.active, p.onActive)
            if (out) Tag("Out", p.bad, p.surface)
        }

        Column(
            Modifier.align(Alignment.Center).alpha(if (out) .45f else 1f)
                .clearAndSetSemantics {
                    contentDescription = "$name, life $life"
                    liveRegion = LiveRegionMode.Polite
                },
            horizontalAlignment = Alignment.CenterHorizontally,
        ) {
            Text("$life", color = p.text, fontSize = lifeSize.sp, fontWeight = FontWeight.Bold, lineHeight = lifeSize.sp)
            Text(
                if (pendingDelta == 0) " " else (if (pendingDelta > 0) "+" else "−") + kotlin.math.abs(pendingDelta),
                color = if (pendingDelta > 0) p.good else p.bad,
                style = MaterialTheme.typography.titleMedium,
            )
            CommanderSummary(unit, summary)
        }

        Row(
            Modifier.align(Alignment.BottomStart).fillMaxWidth().padding(horizontal = 10.dp, vertical = 8.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(10.dp),
        ) {
            TextButton(onClick = onToggleDrawer) { Text("More", color = p.text, fontWeight = FontWeight.SemiBold) }
            val actor = unit.members.firstOrNull { it.playerNumber == summary.activePlayerNumber }
            if (actor != null && !out && summary.state == TableState.RUNNING) {
                val passing = summary.pending.passPlayer != null && unit.members.any { it.playerNumber == summary.pending.passPlayer }
                HoldToPass(passing, { onControl(actor.seat(), "pass") }, haptics, Modifier.weight(1f))
            } else {
                Spacer(Modifier.weight(1f))
            }
            if (active) TurnClock(summary, nowMs)
        }

        Prompt(unit, summary, onControl, onRespond, Modifier.align(Alignment.BottomCenter))
        if (drawerOpen) Drawer(unit, summary, onToggleDrawer, onControl, onCommander)
    }
}

@Composable
private fun Tag(text: String, background: Color, color: Color) {
    Text(
        text,
        color = color,
        style = MaterialTheme.typography.labelMedium,
        fontWeight = FontWeight.Bold,
        modifier = Modifier.background(background, RoundedCornerShape(8.dp)).padding(horizontal = 8.dp, vertical = 2.dp),
    )
}

/** Half the panel: tap for one step, hold to repeat the big step. */
@Composable
private fun HoldHalf(
    glyph: String,
    label: String,
    enabled: Boolean,
    onStep: () -> Unit,
    onHold: () -> Unit,
    glyphAlignment: Alignment,
    modifier: Modifier,
) {
    val p = palette
    val step by rememberUpdatedState(onStep)
    val hold by rememberUpdatedState(onHold)
    val scope = rememberCoroutineScope()
    var pressed by remember { mutableStateOf(false) }
    Box(
        modifier
            .background(if (pressed) p.text.copy(alpha = .07f) else Color.Transparent)
            .semantics {
                contentDescription = label
                role = Role.Button
                if (enabled) onClick { step(); true } else disabled()
            }
            .pointerInput(enabled) {
                if (!enabled) return@pointerInput
                awaitEachGesture {
                    awaitFirstDown(requireUnconsumed = false)
                    pressed = true
                    var held = false
                    val repeating = scope.launch {
                        delay(HOLD_MS)
                        held = true
                        while (true) {
                            hold()
                            delay(REPEAT_MS)
                        }
                    }
                    val up = waitForUpOrCancellation()
                    repeating.cancel()
                    pressed = false
                    if (up != null && !held) {
                        up.consume()
                        step()
                    }
                }
            },
        contentAlignment = glyphAlignment,
    ) {
        Text(glyph, color = p.muted.copy(alpha = .7f), fontSize = 34.sp, modifier = Modifier.padding(horizontal = 22.dp, vertical = 6.dp))
    }
}

/** Hold to pass, so a brushed panel never ends a turn; while Atlas holds the pass, a tap keeps the turn. */
@Composable
private fun HoldToPass(passing: Boolean, onPass: () -> Unit, haptics: TurnHubHaptics, modifier: Modifier) {
    val p = palette
    val pass by rememberUpdatedState(onPass)
    val scope = rememberCoroutineScope()
    var holding by remember { mutableStateOf(false) }
    val shape = RoundedCornerShape(14.dp)
    val label = if (passing) "Passing… tap to keep" else "Hold to pass"
    Box(
        modifier
            .clip(shape)
            .background(if (holding || passing) p.active else p.activeSoft)
            .border(1.dp, p.active, shape)
            .semantics {
                contentDescription = if (passing) "Keep the turn" else "Pass the turn"
                role = Role.Button
                onClick { pass(); true }
            }
            .pointerInput(passing) {
                awaitEachGesture {
                    awaitFirstDown(requireUnconsumed = false)
                    if (passing) {
                        if (waitForUpOrCancellation() != null) pass()
                        return@awaitEachGesture
                    }
                    holding = true
                    val timer = scope.launch {
                        delay(PASS_HOLD_MS)
                        holding = false
                        haptics.confirm()
                        pass()
                    }
                    waitForUpOrCancellation()
                    timer.cancel()
                    holding = false
                }
            }
            .padding(vertical = 12.dp),
        contentAlignment = Alignment.Center,
    ) {
        Text(label, color = if (holding || passing) p.onActive else p.text, fontWeight = FontWeight.SemiBold)
    }
}

@Composable
private fun TurnClock(summary: TableSummary, nowMs: Long) {
    val p = palette
    val remaining = TableClock.turnRemainingMs(summary, nowMs)
    val text = TableClock.format(remaining ?: TableClock.turnElapsedMs(summary, nowMs))
    val color = when (summary.turnTimer.phase) {
        TurnTimerPhase.WARNING -> p.warn
        TurnTimerPhase.EXPIRED -> p.bad
        else -> p.text
    }
    Text(text, color = color, style = MaterialTheme.typography.titleMedium, fontWeight = FontWeight.SemiBold,
        modifier = Modifier.semantics { contentDescription = if (remaining != null) "Turn time left $text" else "Turn time $text" })
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun CommanderSummary(unit: TableUnit, summary: TableSummary) {
    val p = palette
    if (summary.settings.profile != GameProfile.MTG_COMMANDER) return
    val rows = unit.members.flatMap { m -> m.commanderDamage.map { it.sourcePlayer to it.damage.sum() } }.filter { it.second > 0 }
    if (rows.isEmpty()) return
    FlowRow(horizontalArrangement = Arrangement.spacedBy(6.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
        rows.forEach { (source, total) ->
            val from = summary.players.firstOrNull { it.playerNumber == source }?.label ?: "P$source"
            val lethal = total >= 21
            Text(
                "${from.take(8)} $total",
                color = if (lethal) p.bad else p.text,
                style = MaterialTheme.typography.labelMedium,
                fontWeight = if (lethal) FontWeight.Bold else FontWeight.Medium,
                modifier = Modifier
                    .background(p.surface.copy(alpha = .8f), RoundedCornerShape(8.dp))
                    .border(1.dp, if (lethal) p.bad else p.line, RoundedCornerShape(8.dp))
                    .padding(horizontal = 8.dp, vertical = 2.dp),
            )
        }
    }
}

/** Win confirmations and phone life requests land on the panel that answers them. */
@Composable
private fun Prompt(
    unit: TableUnit,
    summary: TableSummary,
    onControl: (TabletSeat, String) -> Unit,
    onRespond: (TabletSeat, Long, Boolean) -> Unit,
    modifier: Modifier,
) {
    val p = palette
    val pending = summary.pending
    val confirmer = unit.members.firstOrNull { it.playerNumber == pending.winConfirmationPlayer }
    val labelOf: (Int) -> String = { n -> summary.players.firstOrNull { it.playerNumber == n }?.label ?: "A player" }
    val (text, onAnswer) = when {
        confirmer != null && pending.winClaimPlayer != null ->
            "${labelOf(pending.winClaimPlayer)} claims the win." to { yes: Boolean -> onControl(confirmer.seat(), if (yes) "confirm" else "deny") }
        else -> {
            val asked = unit.members.firstOrNull { m ->
                m.lifeRequest?.let { it.state == LifeRequestState.PENDING && it.target == m.playerNumber } == true
            } ?: return
            val request = asked.lifeRequest ?: return
            val delta = (if (request.delta > 0) "+" else "") + request.delta
            "${labelOf(request.actor)} asks to change ${asked.label}'s life by $delta." to
                { yes: Boolean -> onRespond(asked.seat(), request.id, yes) }
        }
    }
    val isWin = confirmer != null && pending.winClaimPlayer != null
    BrassCard(modifier.padding(10.dp).widthIn(max = 420.dp).pointerInput(Unit) { detectTapGestures { } }, highlight = p.warn) {
        Text(text, color = p.text, modifier = Modifier.semantics { liveRegion = LiveRegionMode.Assertive })
        Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
            ToneButton(if (isWin) "Deny" else "Reject", { onAnswer(false) }, Modifier.weight(1f), tone = Tone.BAD)
            AccentButton(if (isWin) "Confirm" else "Accept", { onAnswer(true) }, Modifier.weight(1f))
        }
    }
}

/** The panel's drawer: Commander damage received, claiming the win and conceding. It faces the player too. */
@Composable
private fun Drawer(
    unit: TableUnit,
    summary: TableSummary,
    onClose: () -> Unit,
    onControl: (TabletSeat, String) -> Unit,
    onCommander: (TabletSeat, Int, Int, Int) -> Unit,
) {
    val p = palette
    val partners = remember { mutableStateListOf<Int>() }
    Column(
        Modifier
            .fillMaxSize()
            .background(p.surface.copy(alpha = .98f))
            // Keeps taps on the drawer from reaching the life halves underneath.
            .pointerInput(Unit) { detectTapGestures { } }
            .verticalScroll(rememberScrollState())
            .padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(10.dp),
    ) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text(unit.name, color = p.text, style = MaterialTheme.typography.titleLarge, modifier = Modifier.weight(1f))
            TextButton(onClick = onClose) { Text("Close", color = p.text) }
        }
        // Claim the win and concede first, so a long Commander list never hides them.
        val actor = unit.members.firstOrNull { it.playerNumber == summary.activePlayerNumber }
        if (actor != null && summary.state == TableState.RUNNING) {
            ArmedButton("Claim the win", "Tap again to claim", Tone.GOOD) { onControl(actor.seat(), "win") }
        }
        if (summary.state == TableState.RUNNING || summary.state == TableState.PAUSED) {
            unit.members.filter { !it.eliminated }.forEach { member ->
                ArmedButton(
                    if (unit.members.size > 1) "${member.label} concedes" else "Concede",
                    "Tap again to concede",
                    Tone.BAD,
                ) { onControl(member.seat(), "concede") }
            }
        }
        if (summary.settings.profile == GameProfile.MTG_COMMANDER) {
            unit.members.filter { !it.eliminated }.forEach { member ->
                Text(
                    if (unit.members.size > 1) "Commander damage to ${member.label}" else "Commander damage received",
                    color = p.muted,
                    style = MaterialTheme.typography.labelLarge,
                )
                val showPartner = member.playerNumber in partners
                summary.players.filter { it.playerNumber != member.playerNumber }.forEach { source ->
                    val damage = member.commanderDamage.firstOrNull { it.sourcePlayer == source.playerNumber }?.damage.orEmpty()
                    val commanders = if (showPartner || (damage.getOrNull(1) ?: 0) > 0) listOf(1, 2) else listOf(1)
                    commanders.forEach { c ->
                        val value = damage.getOrNull(c - 1) ?: 0
                        val label = source.label + if (commanders.size > 1) " ($c)" else ""
                        Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                            Text(label, color = p.text, modifier = Modifier.weight(1f), maxLines = 1, overflow = TextOverflow.Ellipsis)
                            StepButton("−", "Remove 1 Commander damage from $label", value > 0) {
                                onCommander(member.seat(), source.playerNumber, c, -1)
                            }
                            Text(
                                "$value",
                                color = if (value >= 21) p.bad else p.text,
                                fontWeight = FontWeight.Bold,
                                textAlign = TextAlign.Center,
                                modifier = Modifier.width(40.dp),
                            )
                            StepButton("+", "Add 1 Commander damage from $label", true) {
                                onCommander(member.seat(), source.playerNumber, c, 1)
                            }
                        }
                    }
                }
                TextButton(onClick = {
                    if (showPartner) partners.remove(member.playerNumber) else partners.add(member.playerNumber)
                }) { Text(if (showPartner) "Hide partner commanders" else "Partner commanders", color = p.muted) }
            }
        }
    }
}

@Composable
private fun StepButton(glyph: String, label: String, enabled: Boolean, onClick: () -> Unit) {
    val p = palette
    Box(
        Modifier
            .size(48.dp)
            .clip(CircleShape)
            .background(if (enabled) p.surface3 else p.surface2)
            .border(1.dp, p.line, CircleShape)
            .clickable(enabled = enabled, onClickLabel = label, role = Role.Button, onClick = onClick)
            .semantics { contentDescription = label },
        contentAlignment = Alignment.Center,
    ) {
        Text(glyph, color = if (enabled) p.text else p.faint, fontSize = 22.sp)
    }
}

/** A decisive button: the first tap arms it for three seconds, the second acts. */
@Composable
private fun ArmedButton(label: String, armedLabel: String, tone: Tone, onConfirm: () -> Unit) {
    var armed by remember { mutableStateOf(false) }
    LaunchedEffect(armed) {
        if (armed) {
            delay(3_000)
            armed = false
        }
    }
    ToneButton(
        if (armed) armedLabel else label,
        {
            if (armed) {
                armed = false
                onConfirm()
            } else {
                armed = true
            }
        },
        Modifier.fillMaxWidth(),
        tone = tone,
    )
}
