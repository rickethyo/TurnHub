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
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.LocalTextStyle
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
import com.turnhub.android.data.OfflineChange
import com.turnhub.android.data.TableControls
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
import com.turnhub.android.ui.components.MagicCountersEditor
import com.turnhub.android.ui.components.CounterChips
import androidx.compose.foundation.layout.heightIn
import androidx.compose.animation.core.RepeatMode
import androidx.compose.animation.core.animateFloat
import androidx.compose.animation.core.animateIntAsState
import androidx.compose.animation.core.infiniteRepeatable
import androidx.compose.animation.core.rememberInfiniteTransition
import androidx.compose.animation.core.snap
import androidx.compose.animation.core.tween
import androidx.compose.ui.graphics.Brush
import androidx.activity.compose.BackHandler
import androidx.compose.ui.draw.drawWithContent
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.drawscope.Stroke
import com.turnhub.android.ui.components.EliminationRulesEditor
import com.turnhub.android.ui.components.MonarchBadge
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
    /** Sends one request for a seat: to Atlas, or (standalone) to the app's own game. */
    controls: (suspend TableControls.() -> Unit) -> Unit,
    onDismissMessage: () -> Unit,
    reduceMotion: Boolean,
    onClose: () -> Unit,
    /** Atlas isn't answering: life and Commander taps are kept for it, everything else waits. */
    offline: Boolean = false,
    /** The standalone game (no Atlas): the menu has no tablet grant to hand back. */
    standalone: Boolean = false,
) {
    val p = palette
    val haptics = rememberHaptics()
    val isOffline by rememberUpdatedState(offline)
    val scope = rememberCoroutineScope()
    val current by rememberUpdatedState(summary)
    // Life taps gather per panel and go to Atlas once after a short pause.
    val unsent = remember { mutableStateMapOf<String, Int>() }
    val sent = remember { mutableStateListOf<SentLife>() }
    val gathering = remember { mutableMapOf<String, Job>() }
    var nextId by remember { mutableStateOf(0L) }
    // One player's menu at a time, full screen and turned to face that player.
    var playerMenu by remember { mutableStateOf<String?>(null) }
    var flipped by rememberSaveable { mutableStateOf(false) }
    var menuOpen by remember { mutableStateOf(false) }
    BackHandler(enabled = playerMenu != null || menuOpen) {
        playerMenu = null
        menuOpen = false
    }

    LaunchedEffect(summary.revision) { sent.removeAll { it.acked && it.sentAtRevision != summary.revision } }
    LaunchedEffect(tablet.message) {
        if (tablet.message != null) {
            delay(3_500)
            onDismissMessage()
        }
    }

    fun send(seat: TabletSeat?, action: String) {
        seat ?: return
        haptics.tick()
        if (isOffline) controls { refuseOffline() } else controls { this.control(seat, action) }
    }

    /** Keeps a change for Atlas while it isn't answering, against the last state it sent. */
    fun keep(player: TablePlayer, delta: Int, source: Int? = null, commander: Int = 1) {
        val change = OfflineChange(
            participantId = player.participantId,
            source = source,
            commander = commander,
            delta = delta,
            queuedAtMs = TableClock.nowMs(),
            bootId = current.bootId,
            gameElapsedMs = current.gameElapsedMs,
            game = current.game,
        )
        controls { queueOffline(change) }
    }

    fun flush(key: String) {
        val delta = unsent.remove(key) ?: return
        if (delta == 0) return
        val unit = tableUnits(current).firstOrNull { it.key == key } ?: return
        if (isOffline) return keep(unit.lifeSeat, delta)
        val entry = SentLife(nextId++, key, delta, current.revision)
        sent.add(entry)
        controls {
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
        fun degreesOf(index: Int) = when {
            landscape -> if (index in plan.far) 180 else 0
            else -> if (index in plan.far) 90 else 270
        }
        val panel: @Composable (Int, Int, Modifier) -> Unit = { index, degrees, modifier ->
            val unit = units[index]
            key(unit.key) {
                val pending = unsent[unit.key] ?: 0
                // An acknowledged change is dropped as soon as a newer snapshot arrives (which carries
                // it), in the same frame, so the number never counts it twice and jumps back.
                val inFlight = sent.filter { it.key == unit.key && !(it.acked && it.sentAtRevision != summary.revision) }
                val life = (unit.lifeSeat.life ?: 0) + pending + inFlight.sumOf { it.delta } +
                    unit.members.sumOf { tablet.offlineLife(it.participantId) }
                Box(modifier.padding(4.dp).facing((degrees + turn) % 360)) {
                    PlayerPanel(
                        unit = unit,
                        summary = summary,
                        life = life,
                        pendingDelta = pending,
                        onOpenMenu = {
                            haptics.tick()
                            playerMenu = unit.key
                        },
                        onBump = { bump(unit, it) },
                        onControl = { seat, action -> send(seat, action) },
                        onRespond = { seat, id, accept ->
                            if (isOffline) controls { refuseOffline() }
                            else controls { this.respondLife(seat, id, accept) }
                        },
                        offlineCommander = tablet::offlineCommander,
                        haptics = haptics,
                        reduceMotion = reduceMotion,
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

        Hub(summary, Modifier.align(Alignment.Center)) {
            haptics.tick()
            menuOpen = true
        }

        val menuIndex = units.indexOfFirst { it.key == playerMenu }
        if (menuIndex < 0 || summary.state == TableState.GAME_OVER) {
            if (playerMenu != null) playerMenu = null
        } else {
            val unit = units[menuIndex]
            Box(Modifier.fillMaxSize().facing((degreesOf(menuIndex) + turn) % 360)) {
                PlayerMenu(
                    unit = unit,
                    summary = summary,
                    onClose = { playerMenu = null },
                    onControl = { seat, action ->
                        if (action == "concede" || action == "eliminate" || action == "win") playerMenu = null
                        send(seat, action)
                    },
                    onCommander = { member, source, which, delta ->
                        haptics.tick()
                        if (isOffline) keep(member, delta, source, which)
                        else controls { this.commander(member.seat(), source, which, delta) }
                    },
                    onCounter = { member, kind, value, commander ->
                        if (isOffline) controls { refuseOffline() }
                        else controls { counter(member.seat(), kind, value, commander) }
                    },
                    offlineCommander = tablet::offlineCommander,
                )
            }
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
            TableState.PAUSED -> {
                // A win claim pauses the game too. Keep the confirming player's
                // panel reachable; Atlas requires an answer before resuming.
                if (summary.pending.winClaimPlayer == null && summary.pending.winConfirmationPlayer == null) {
                    if (summary.pending.eliminationTargetPlayer != null) {
                        Overlay("Player removal pending", "Confirm or cancel the removal on Atlas or the selecting Sigil.") {}
                    } else {
                        Overlay("Paused", "The clocks are stopped.") {
                            EliminationRulesEditor(summary.settings, !isOffline && anySeat != null) { field, value ->
                                anySeat?.let { seat -> controls { setEliminationRule(seat, field, value) } }
                            }
                            if (summary.settings.profile == GameProfile.MTG || summary.settings.profile == GameProfile.MTG_COMMANDER) {
                                summary.players.filter { !it.eliminated }.forEach { member ->
                                    ToneButton("${member.label}: I’m the monarch", { send(member.seat(), "monarch") },
                                        Modifier.fillMaxWidth(), tone = Tone.ACCENT,
                                        enabled = !isOffline && member.playerNumber != summary.monarchPlayerNumber)
                                }
                            }
                            PausedMagicCounters(summary, !isOffline) { member, kind, value, commander ->
                                controls { counter(member.seat(), kind, value, commander) }
                            }
                            AccentButton("Resume", { send(activeSeat, "pause") }, Modifier.fillMaxWidth())
                        }
                    }
                }
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
                Overlay("Table", null, onDismiss = { menuOpen = false }) {
                    GameClockLine(summary)
                    TableToolsCard(summary.players.filter { !it.eliminated }.map { it.label }, reduceMotion) { haptics.tick() }
                    EliminationRulesEditor(summary.settings, !isOffline && anySeat != null) { field, value ->
                        anySeat?.let { seat -> controls { setEliminationRule(seat, field, value) } }
                    }
                    ToneButton("Pause", { menuOpen = false; send(activeSeat, "pause") }, Modifier.fillMaxWidth())
                    ToneButton("Turn panels around", { flipped = !flipped; menuOpen = false }, Modifier.fillMaxWidth())
                    ToneButton("Back to the app", { menuOpen = false; onClose() }, Modifier.fillMaxWidth())
                    if (standalone) {
                        ArmedButton("End without a winner", "Tap again to end the game", Tone.BAD) {
                            menuOpen = false
                            send(anySeat, "draw")
                        }
                    } else {
                        ToneButton("Leave tablet mode", {
                            menuOpen = false
                            if (isOffline) {
                                controls { refuseOffline() }
                            } else {
                                controls { this.disable() }
                                onClose()
                            }
                        }, Modifier.fillMaxWidth(), tone = Tone.BAD)
                    }
                    AccentButton("Close", { menuOpen = false }, Modifier.fillMaxWidth())
                }
            }
        }
    }
}

/** The centre disc: the game clock, and the table menu on tap. */
@Composable
private fun Hub(summary: TableSummary, modifier: Modifier, onOpen: () -> Unit) {
    val p = palette
    // Ticks here only, so the clock never redraws the panels around it.
    val nowMs = rememberNowMs(ticking = summary.state == TableState.RUNNING, intervalMs = 500)
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
    text: String?,
    onDismiss: (() -> Unit)? = null,
    content: @Composable () -> Unit,
) = OverlayFrame(title, text, onDismiss, content)

@Composable
private fun OverlayButtons(content: @Composable RowScope.() -> Unit) =
    Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(10.dp), content = content)

@Composable
private fun OverlayFrame(title: String, text: String?, onDismiss: (() -> Unit)?, content: @Composable () -> Unit) {
    val p = palette
    Box(
        Modifier
            .fillMaxSize()
            .background(p.scrim)
            // Swallows touches so nothing under the card changes; a tap outside closes the menu.
            .pointerInput(onDismiss) { detectTapGestures { onDismiss?.invoke() } },
        contentAlignment = Alignment.Center,
    ) {
        BrassCard(Modifier.widthIn(max = 380.dp).padding(16.dp).verticalScroll(rememberScrollState()).pointerInput(Unit) { detectTapGestures { } }) {
            Text(title, color = p.text, style = MaterialTheme.typography.headlineSmall)
            text?.let { Text(it, color = p.muted) }
            Column(verticalArrangement = Arrangement.spacedBy(8.dp)) { content() }
        }
    }
}

@Composable
private fun GameClockLine(summary: TableSummary) {
    val nowMs = rememberNowMs(ticking = summary.state == TableState.RUNNING, intervalMs = 500)
    Text("Game clock ${TableClock.format(TableClock.gameElapsedMs(summary, nowMs))}", color = palette.muted)
}

@Composable
private fun PausedMagicCounters(summary: TableSummary, enabled: Boolean, onCounter: (TablePlayer, String, Int, Int) -> Unit) {
    if (summary.settings.profile != GameProfile.MTG && summary.settings.profile != GameProfile.MTG_COMMANDER) return
    var selected by remember { mutableStateOf<Int?>(null) }
    var expanded by remember { mutableStateOf(false) }
    val living = summary.players.filter { !it.eliminated }
    val player = living.firstOrNull { it.playerNumber == selected } ?: living.firstOrNull() ?: return
    Box {
        TextButton({ expanded = true }) { Text("Counters: ${player.label}") }
        androidx.compose.material3.DropdownMenu(expanded, { expanded = false }) {
            living.forEach { member ->
                androidx.compose.material3.DropdownMenuItem(text = { Text(member.label) }, onClick = { selected = member.playerNumber; expanded = false })
            }
        }
    }
    MagicCountersEditor(player.counters, summary.initiativePlayerNumber == player.playerNumber,
        summary.settings.profile == GameProfile.MTG_COMMANDER, enabled) { kind, value, commander -> onCounter(player, kind, value, commander) }
}

@Composable
private fun PlayerPanel(
    unit: TableUnit,
    summary: TableSummary,
    life: Int,
    pendingDelta: Int,
    onOpenMenu: () -> Unit,
    onBump: (Int) -> Unit,
    onControl: (TabletSeat, String) -> Unit,
    onRespond: (TabletSeat, Long, Boolean) -> Unit,
    offlineCommander: (Long, Int, Int) -> Int,
    haptics: TurnHubHaptics,
    reduceMotion: Boolean = false,
) {
    val p = palette
    val shape = RoundedCornerShape(22.dp)
    val hue = unit.members.first().color?.let { Color(android.graphics.Color.parseColor(it)) }
        ?: p.avatarColor(unit.members.first().playerNumber)
    val active = unit.members.any { summary.hasTurn(it.playerNumber) } && summary.state != TableState.GAME_OVER
    val out = unit.out
    val (small, big) = lifeSteps(summary.settings.profile)
    val name = unit.name
    // The active panel's edge breathes slowly; steady when motion is reduced. The value is
    // read only while drawing, so the pulse redraws the edge without recomposing the panel.
    val glow = if (active && !reduceMotion) {
        rememberInfiniteTransition(label = "turnGlow").animateFloat(
            .55f, 1f, infiniteRepeatable(tween(1_100), RepeatMode.Reverse), label = "glow",
        )
    } else null
    val activeColor = p.active
    BoxWithConstraints(
        Modifier
            .fillMaxSize()
            .clip(shape)
            .background(p.surface)
            // Full-bleed seat color, deeper toward the player's edge, so each seat reads at a glance.
            .background(Brush.verticalGradient(listOf(
                hue.copy(alpha = if (p.dark) .30f else .22f),
                hue.copy(alpha = if (p.dark) .52f else .40f),
            )))
            .then(
                if (!active) Modifier.border(1.dp, p.line, shape)
                else Modifier.drawWithContent {
                    drawContent()
                    val w = 4.dp.toPx()
                    drawRoundRect(
                        activeColor.copy(alpha = glow?.value ?: 1f),
                        topLeft = Offset(w / 2, w / 2),
                        size = Size(size.width - w, size.height - w),
                        cornerRadius = CornerRadius(22.dp.toPx() - w / 2),
                        style = Stroke(w),
                    )
                },
            ),
    ) {
        val nameMax = maxWidth * .6f
        // Short panels (five or six on a phone) put Menu and Hold to pass beside each other in a
        // slim bottom bar, and the number is sized to the room between it and the name.
        val compact = maxHeight < 260.dp
        val between = (maxHeight.value - if (compact) 104f else 120f).coerceAtLeast(24f)
        val lifeSize = minOf(
            minOf(maxWidth, maxHeight).value * if ("$life".length > 3) .22f else .36f,
            between * .8f,
        ).coerceIn(24f, 160f)
        Column(Modifier.fillMaxSize().alpha(if (out) .45f else 1f)) {
            HoldHalf("+", "$name: add $small life", !out, { onBump(small) }, { onBump(big) }, Alignment.CenterEnd, Modifier.weight(1f).fillMaxWidth())
            HoldHalf("−", "$name: subtract $small life", !out, { onBump(-small) }, { onBump(-big) }, Alignment.CenterEnd, Modifier.weight(1f).fillMaxWidth())
        }

        Row(
            Modifier.align(Alignment.TopStart).padding(horizontal = 16.dp, vertical = 12.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            unit.members.forEach { member -> member.avatar?.let {
                com.turnhub.android.ui.components.AvatarGlyph(it, p.text, 32.dp)
            } }
            Text(name, color = p.text, style = MaterialTheme.typography.titleMedium, fontWeight = FontWeight.SemiBold,
                maxLines = 1, overflow = TextOverflow.Ellipsis, modifier = Modifier.widthIn(max = nameMax))
            if (active) Tag("Turn", p.active, p.onActive)
            if (out) Tag(if (unit.members.any { it.conceded }) "Conceded" else "Eliminated", p.bad, p.surface)
        }

        unit.members.firstOrNull { it.playerNumber == summary.monarchPlayerNumber }?.let { monarch ->
            MonarchBadge(if (unit.members.size > 1) monarch.label else null,
                Modifier.align(Alignment.TopStart).padding(start = 16.dp, top = 52.dp))
        }

        Column(
            Modifier.align(Alignment.Center).padding(top = 44.dp, bottom = if (compact) 56.dp else 64.dp)
                .alpha(if (out) .45f else 1f)
                .clearAndSetSemantics {
                    contentDescription = "$name, life $life"
                    liveRegion = LiveRegionMode.Polite
                },
            horizontalAlignment = Alignment.CenterHorizontally,
        ) {
            // The running change floats as a pill over the number's top edge, so it never moves it.
            val pill = if (pendingDelta == 0) null else (if (pendingDelta > 0) "+" else "−") + kotlin.math.abs(pendingDelta)
            Box(contentAlignment = Alignment.Center) {
                LifeNumber(life, lifeSize, reduceMotion)
                if (pill != null) {
                    val tone = if (pendingDelta > 0) p.good else p.bad
                    Text(pill, color = tone, style = MaterialTheme.typography.titleMedium, fontWeight = FontWeight.ExtraBold,
                        modifier = Modifier
                            .align(Alignment.TopEnd)
                            .offset(x = 18.dp, y = (-10).dp)
                            .background(p.surface.copy(alpha = .9f), RoundedCornerShape(99.dp))
                            .border(1.dp, tone.copy(alpha = .6f), RoundedCornerShape(99.dp))
                            .padding(horizontal = 10.dp, vertical = 1.dp))
                }
            }
            CommanderSummary(unit, summary, offlineCommander)
            if (summary.settings.profile == GameProfile.MTG || summary.settings.profile == GameProfile.MTG_COMMANDER) {
                unit.members.forEach { member ->
                    CounterChips(member.counters, summary.initiativePlayerNumber == member.playerNumber,
                        summary.settings.profile == GameProfile.MTG_COMMANDER, summary.settings.poisonLimit,
                        Modifier.padding(top = 4.dp), compact = true)
                }
            }
        }

        Row(
            Modifier.align(Alignment.BottomStart).fillMaxWidth().padding(horizontal = 10.dp, vertical = 8.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(10.dp),
        ) {
            MenuButton(name, compact, onOpenMenu)
            val actor = unit.members.firstOrNull { it.playerNumber == summary.activePlayerNumber }
            if (actor != null && !out && summary.state == TableState.RUNNING) {
                val passing = summary.pending.passPlayer != null && unit.members.any { it.playerNumber == summary.pending.passPlayer }
                HoldToPass(passing, { onControl(actor.seat(), "pass") }, haptics, Modifier.weight(1f), compact)
            } else {
                Spacer(Modifier.weight(1f))
            }
            if (active) TurnClock(summary)
        }

        Prompt(unit, summary, onControl, onRespond, Modifier.align(Alignment.BottomCenter))
    }
}

/** The life total, counting to its new value; only this text recomposes while it animates. */
@Composable
private fun LifeNumber(life: Int, size: Float, reduceMotion: Boolean) {
    val shown by animateIntAsState(life, if (reduceMotion) snap() else tween(220), label = "life")
    Text("$shown", color = palette.text, fontSize = size.sp, fontWeight = FontWeight.Black, lineHeight = size.sp)
}

/** Opens the player's menu: a clear, thumb-sized pill rather than a bare word. */
@Composable
private fun MenuButton(name: String, compact: Boolean, onOpen: () -> Unit) {
    val p = palette
    val shape = RoundedCornerShape(99.dp)
    Row(
        Modifier
            .heightIn(min = 48.dp)
            .clip(shape)
            .background(p.surface.copy(alpha = .85f))
            .border(1.dp, p.lineStrong, shape)
            .clickable(onClickLabel = "Open $name's menu", role = Role.Button, onClick = onOpen)
            .padding(horizontal = if (compact) 14.dp else 16.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(6.dp),
    ) {
        Text("☰", color = p.text, fontSize = 18.sp)
        // A short panel keeps just the icon; its spoken label still says Menu.
        if (!compact) Text("Menu", color = p.text, fontWeight = FontWeight.SemiBold)
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
        Text(glyph, color = p.text.copy(alpha = if (pressed) .8f else .35f), fontSize = 44.sp, fontWeight = FontWeight.Light,
            modifier = Modifier.padding(horizontal = 26.dp, vertical = 6.dp))
    }
}

/** Hold to pass, so a brushed panel never ends a turn; while Atlas holds the pass, a tap keeps the turn. */
@Composable
private fun HoldToPass(passing: Boolean, onPass: () -> Unit, haptics: TurnHubHaptics, modifier: Modifier, compact: Boolean = false) {
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
            .heightIn(min = 48.dp)
            .padding(horizontal = 8.dp, vertical = if (compact) 6.dp else 12.dp),
        contentAlignment = Alignment.Center,
    ) {
        Text(label, color = if (holding || passing) p.onActive else p.text, fontWeight = FontWeight.SemiBold,
            maxLines = 1, overflow = TextOverflow.Ellipsis,
            style = if (compact) MaterialTheme.typography.labelLarge else LocalTextStyle.current)
    }
}

@Composable
private fun TurnClock(summary: TableSummary) {
    val p = palette
    val nowMs = rememberNowMs(ticking = summary.state == TableState.RUNNING, intervalMs = 500)
    val expired = summary.turnTimer.phase == TurnTimerPhase.EXPIRED
    val remaining = TableClock.turnRemainingMs(summary, nowMs)
    // Over time shows "+" and the overtime, so time-up never rests on color alone.
    val text = if (expired) "+" + TableClock.format(TableClock.overtimeMs(summary, nowMs))
        else TableClock.format(remaining ?: TableClock.turnElapsedMs(summary, nowMs))
    val color = when (summary.turnTimer.phase) {
        TurnTimerPhase.WARNING -> p.warn
        TurnTimerPhase.EXPIRED -> p.bad
        else -> p.text
    }
    Text(text, color = color, style = MaterialTheme.typography.titleMedium, fontWeight = FontWeight.SemiBold,
        modifier = Modifier.semantics {
            contentDescription = when {
                expired -> "Turn time is up, over by ${text.drop(1)}"
                remaining != null -> "Turn time left $text"
                else -> "Turn time $text"
            }
        })
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun CommanderSummary(unit: TableUnit, summary: TableSummary, offlineCommander: (Long, Int, Int) -> Int) {
    val p = palette
    if (summary.settings.profile != GameProfile.MTG_COMMANDER) return
    val rows = unit.members.flatMap { m ->
        summary.players.filter { it.playerNumber != m.playerNumber }.map { source ->
            val damage = m.commanderDamage.firstOrNull { it.sourcePlayer == source.playerNumber }?.damage.orEmpty()
            source.playerNumber to (1..2).sumOf { c -> (damage.getOrNull(c - 1) ?: 0) + offlineCommander(m.participantId, source.playerNumber, c) }
        }
    }.filter { it.second > 0 }
    if (rows.isEmpty()) return
    FlowRow(horizontalArrangement = Arrangement.spacedBy(6.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
        rows.forEach { (source, total) ->
            val from = summary.players.firstOrNull { it.playerNumber == source }?.label ?: "P$source"
            val lethal = total >= 21
            // Tinted with the source seat's color, so damage is traced at a glance; the name still says who.
            val tint = p.avatarColor(source)
            Text(
                "⚔ ${from.take(8)} $total",
                color = if (lethal) p.bad else p.text,
                style = MaterialTheme.typography.labelMedium,
                fontWeight = if (lethal) FontWeight.Bold else FontWeight.Medium,
                modifier = Modifier
                    .background(tint.copy(alpha = .35f), RoundedCornerShape(8.dp))
                    .border(if (lethal) 2.dp else 1.dp, if (lethal) p.bad else tint, RoundedCornerShape(8.dp))
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

/**
 * A player's menu over the whole table, turned to face that player: counters,
 * Commander damage received, claiming the win and conceding. It closes with
 * the ✕, the Done button at the bottom, or Back.
 */
@Composable
private fun PlayerMenu(
    unit: TableUnit,
    summary: TableSummary,
    onClose: () -> Unit,
    onControl: (TabletSeat, String) -> Unit,
    onCommander: (TablePlayer, Int, Int, Int) -> Unit,
    onCounter: (TablePlayer, String, Int, Int) -> Unit,
    offlineCommander: (Long, Int, Int) -> Int,
) {
    val p = palette
    val partners = remember { mutableStateListOf<Int>() }
    val hue = unit.members.first().color?.let { Color(android.graphics.Color.parseColor(it)) }
        ?: p.avatarColor(unit.members.first().playerNumber)
    Column(
        Modifier
            .fillMaxSize()
            .background(p.bg)
            .background(Brush.verticalGradient(listOf(hue.copy(alpha = .10f), hue.copy(alpha = .28f))))
            // Keeps taps on the menu from reaching the table underneath.
            .pointerInput(Unit) { detectTapGestures { } },
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        Row(
            Modifier.widthIn(max = 640.dp).fillMaxWidth().padding(start = 20.dp, end = 8.dp, top = 8.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Text(unit.name, color = p.text, style = MaterialTheme.typography.headlineSmall, fontWeight = FontWeight.SemiBold,
                maxLines = 1, overflow = TextOverflow.Ellipsis, modifier = Modifier.weight(1f))
            Box(
                Modifier
                    .size(56.dp)
                    .clip(CircleShape)
                    .clickable(onClickLabel = "Close ${unit.name}'s menu", role = Role.Button, onClick = onClose),
                contentAlignment = Alignment.Center,
            ) { Text("✕", color = p.text, fontSize = 26.sp) }
        }
        Column(
            Modifier
                .weight(1f)
                .widthIn(max = 640.dp)
                .fillMaxWidth()
                .verticalScroll(rememberScrollState())
                .padding(horizontal = 20.dp, vertical = 8.dp),
            verticalArrangement = Arrangement.spacedBy(10.dp),
        ) {
            PlayerMenuItems(unit, summary, partners, onControl, onCommander, onCounter, offlineCommander)
        }
        AccentButton("Done", onClose, Modifier.widthIn(max = 640.dp).fillMaxWidth().padding(horizontal = 20.dp, vertical = 12.dp))
    }
}

@Composable
private fun PlayerMenuItems(
    unit: TableUnit,
    summary: TableSummary,
    partners: MutableList<Int>,
    onControl: (TabletSeat, String) -> Unit,
    onCommander: (TablePlayer, Int, Int, Int) -> Unit,
    onCounter: (TablePlayer, String, Int, Int) -> Unit,
    offlineCommander: (Long, Int, Int) -> Int,
) {
    val p = palette
    run {
        // Claim the win and concede first, so a long Commander list never hides them.
        val actor = unit.members.firstOrNull { it.playerNumber == summary.activePlayerNumber }
        if (actor != null && summary.state == TableState.RUNNING) {
            ArmedButton("Claim the win", "Tap again to claim", Tone.GOOD) { onControl(actor.seat(), "win") }
        }
        if (summary.state == TableState.RUNNING || summary.state == TableState.PAUSED) {
            unit.members.filter { !it.eliminated }.forEach { member ->
                if (summary.settings.profile == GameProfile.MTG || summary.settings.profile == GameProfile.MTG_COMMANDER) {
                    Text(if (unit.members.size > 1) "${member.label} counters and status" else "Counters and status", color = p.text)
                    MagicCountersEditor(member.counters, summary.initiativePlayerNumber == member.playerNumber,
                        summary.settings.profile == GameProfile.MTG_COMMANDER) { kind, value, commander -> onCounter(member, kind, value, commander) }
                    ToneButton(
                        if (unit.members.size > 1) "${member.label}: I’m the monarch" else "I’m the monarch",
                        { onControl(member.seat(), "monarch") }, Modifier.fillMaxWidth(), tone = Tone.ACCENT,
                        enabled = member.playerNumber != summary.monarchPlayerNumber,
                    )
                }
                ArmedButton(
                    if (unit.members.size > 1) "${member.label} concedes" else "Concede",
                    "Tap again to concede",
                    Tone.BAD,
                ) { onControl(member.seat(), "concede") }
                ArmedButton(
                    if (unit.members.size > 1) "Eliminate ${member.label}" else "Eliminate",
                    "Tap again to eliminate", Tone.BAD,
                ) { onControl(member.seat(), "eliminate") }
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
                    fun valueOf(c: Int) = (damage.getOrNull(c - 1) ?: 0) + offlineCommander(member.participantId, source.playerNumber, c)
                    val commanders = if (showPartner || valueOf(2) > 0) listOf(1, 2) else listOf(1)
                    commanders.forEach { c ->
                        val value = valueOf(c)
                        val label = source.label + if (commanders.size > 1) " ($c)" else ""
                        Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                            Text(label, color = p.text, modifier = Modifier.weight(1f), maxLines = 1, overflow = TextOverflow.Ellipsis)
                            StepButton("−", "Remove 1 Commander damage from $label", value > 0) {
                                onCommander(member, source.playerNumber, c, -1)
                            }
                            Text(
                                "$value",
                                color = if (value >= 21) p.bad else p.text,
                                fontWeight = FontWeight.Bold,
                                textAlign = TextAlign.Center,
                                modifier = Modifier.width(40.dp),
                            )
                            StepButton("+", "Add 1 Commander damage from $label", true) {
                                onCommander(member, source.playerNumber, c, 1)
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
