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
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.wrapContentHeight
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.LocalTextStyle
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.setValue
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.key
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.mutableStateMapOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.saveable.listSaver
import androidx.compose.runtime.toMutableStateList
import androidx.compose.runtime.snapshots.SnapshotStateList
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.clipToBounds
import androidx.compose.ui.draw.rotate
import com.turnhub.android.standalone.StandaloneGame
import com.turnhub.android.domain.CooperativeTurn
import com.turnhub.android.domain.InvestigatorCard
import com.turnhub.android.domain.InvestigatorCardSlot
import com.turnhub.android.domain.cooperativeTurn
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.input.pointer.AwaitPointerEventScope
import androidx.compose.ui.input.pointer.PointerEventPass
import androidx.compose.ui.input.pointer.PointerInputChange
import androidx.compose.ui.input.key.onPreviewKeyEvent
import androidx.compose.ui.layout.layout
import androidx.compose.ui.semantics.LiveRegionMode
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.clearAndSetSemantics
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.disabled
import androidx.compose.ui.semantics.liveRegion
import androidx.compose.ui.semantics.onClick
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.semantics.role
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Constraints
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.turnhub.android.data.OfflineChange
import com.turnhub.android.data.PLANAR_PREFIX
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
import com.turnhub.android.ui.components.TeamPoisonRow
import com.turnhub.android.ui.components.CounterChips
import com.turnhub.android.ui.components.counterChips
import androidx.compose.foundation.layout.heightIn
import androidx.compose.animation.core.RepeatMode
import androidx.compose.animation.core.animateFloat
import androidx.compose.animation.core.animateIntAsState
import androidx.compose.animation.core.infiniteRepeatable
import androidx.compose.animation.core.rememberInfiniteTransition
import androidx.compose.animation.core.snap
import androidx.compose.animation.core.tween
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.asAndroidBitmap
import androidx.compose.ui.graphics.toArgb
import androidx.compose.ui.graphics.layer.drawLayer
import androidx.compose.ui.graphics.rememberGraphicsLayer
import androidx.activity.compose.BackHandler
import androidx.compose.ui.draw.drawWithContent
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.drawscope.Stroke
import com.turnhub.android.ui.components.EliminationRulesEditor
import com.turnhub.android.ui.components.MonarchBadge
import com.turnhub.android.ui.components.BrassCard
import com.turnhub.android.ui.components.DisclosureCard
import com.turnhub.android.ui.components.Tone
import com.turnhub.android.ui.components.ToneButton
import com.turnhub.android.ui.components.TurnHubHaptics
import com.turnhub.android.ui.components.rememberHaptics
import com.turnhub.android.ui.components.rememberNowMs
import com.turnhub.android.domain.StarCommander
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

/** Display selection never removes a player whose win confirmation is required. */
internal fun gameCardUnits(
    all: List<TableUnit>, solo: String?, selected: List<String>, confirmer: Int?,
): List<TableUnit> {
    val validSolo = solo?.takeIf { key -> all.any { it.key == key } }
    val validSelected = selected.filter { key -> all.any { it.key == key } }
    return all.filter { unit ->
        unit.members.any { it.playerNumber == confirmer } || when {
            validSolo != null -> unit.key == validSolo
            validSelected.isNotEmpty() -> unit.key in validSelected
            else -> true
        }
    }
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
    when (profile) {
        GameProfile.YUGIOH -> 100 to 1000
        // Pokémon counts Prize cards, Lorcana lore and Death May Die wounds, one at a time.
        GameProfile.POKEMON, GameProfile.LORCANA, GameProfile.DEATH_MAY_DIE -> 1 to 1
        else -> 1 to 5
    }

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
    /** Local rule authority: applies local countdowns/cooperative rules; Atlas supplies its own. */
    standalone: Boolean = false,
    /** Optional full-record caption override; otherwise share the host's finished snapshot. */
    onShareResult: ((android.graphics.Bitmap) -> Unit)? = null,
    /** Shares the finished game's record as CSV, one row per player; null hides the button. */
    onShareCsv: (() -> Unit)? = null,
    /** The finished game was under a minute and isn't counted until [onCountGame]. */
    shortGameUnsaved: Boolean = summary.shortGameUncounted,
    onCountGame: () -> Unit = { summary.anySeat()?.let { seat -> controls { control(seat, "count-game") } } },
    onPlanarRoll: (() -> Unit)? = null,
    /** Observe the actual game without exposing controls before authorization. */
    readOnly: Boolean = false,
    phoneView: Boolean = false,
) {
    val p = palette
    val haptics = rememberHaptics()
    val isOffline by rememberUpdatedState(offline)
    val scope = rememberCoroutineScope()
    val context = androidx.compose.ui.platform.LocalContext.current
    val current by rememberUpdatedState(summary)
    // Life taps gather per panel and go to Atlas once after a short pause.
    val unsent = remember { mutableStateMapOf<String, Int>() }
    val sent = remember { mutableStateListOf<SentLife>() }
    val gathering = remember { mutableMapOf<String, Job>() }
    var nextId by remember { mutableStateOf(0L) }
    // One player's menu at a time, full screen and turned to face that player.
    var playerMenu by remember { mutableStateOf<String?>(null) }
    var manualRemoval by rememberSaveable(summary.bootId, summary.game) { mutableStateOf<Int?>(null) }
    var passToTurn by remember { mutableStateOf<CooperativeTurn?>(null) }
    val cooperativeTurn = summary.cooperativeTurn()
    LaunchedEffect(cooperativeTurn, summary.nextTurnPlayerNumber) {
        if (passToTurn != cooperativeTurn || summary.nextTurnPlayerNumber != null) passToTurn = null
    }
    val partners = rememberSaveable(summary.atlasId, summary.bootId, summary.game,
        saver = listSaver<SnapshotStateList<Int>, Int>(save = { it.toList() }, restore = { it.toMutableStateList() })) {
        mutableStateListOf<Int>()
    }
    var flipped by rememberSaveable { mutableStateOf(false) }
    // The phone-hosted table can show one player's panel, for a group with no centre display
    // whose host plays from their own device like everyone else (owner, 2026-10-20).
    var soloKey by rememberSaveable(summary.bootId, summary.game) { mutableStateOf<String?>(null) }
    val passKeys = rememberSaveable(summary.bootId, summary.game,
        saver = listSaver<SnapshotStateList<String>, String>(save = { it.toList() }, restore = { it.toMutableStateList() })) {
        mutableStateListOf<String>()
    }
    val starLayout = rememberStarLayout()
    var menuOpen by remember { mutableStateOf(false) }
    // Atlas's Planechase with the players' own planes: the app rolls the die and Atlas counts.
    val atlasPlanes = summary.atlasPlanechase.takeIf { !standalone }
    var atlasRolling by remember { mutableStateOf(false) }
    val planarRoll = onPlanarRoll ?: if (atlasPlanes != null && atlasPlanes.plane?.phenomenon != true &&
        summary.state == TableState.RUNNING && !readOnly && !offline) {
        { atlasRolling = true }
    } else null
    var bagPrompt by rememberBagPromptState(summary)
    val bag = summary.doomBag
    val attribution = summary.players.firstOrNull { it.eliminationPending }
    var sourceOpen by rememberSaveable(summary.bootId, summary.game, attribution?.playerNumber) { mutableStateOf(true) }
    val showBag = !readOnly && bag != null && bagPrompt.shouldShow(bag)
    fun dismissBag() { bagPrompt = bagPrompt.dismiss(bag) }
    val focus = androidx.compose.ui.platform.LocalFocusManager.current
    LaunchedEffect(readOnly) {
        if (readOnly) {
            focus.clearFocus(force = true)
            playerMenu = null
            menuOpen = false
            manualRemoval = null
            passToTurn = null
            gathering.values.forEach { it.cancel() }
            gathering.clear()
            unsent.clear()
            sent.clear()
        }
    }
    BackHandler(enabled = passToTurn != null || playerMenu != null || menuOpen || showBag) {
        if (passToTurn != null) passToTurn = null
        else if (playerMenu != null || menuOpen) {
            playerMenu = null
            menuOpen = false
        } else dismissBag()
    }

    LaunchedEffect(summary.revision) { sent.removeAll { it.acked && it.sentAtRevision != summary.revision } }
    LaunchedEffect(tablet.message) {
        if (tablet.message != null) {
            delay(3_500)
            onDismissMessage()
        }
    }

    var pendingStart by remember { mutableStateOf<Pair<TabletSeat, String>?>(null) }
    val startCountdown = rememberGameStartCountdown(summary.state, standalone && !offline) {
        pendingStart?.let { (seat, action) -> controls { control(seat, action) } }
        pendingStart = null
    }
    fun send(seat: TabletSeat?, action: String) {
        if (readOnly) return
        seat ?: return
        if (standalone && action == "pass" && current.settings.profile.cooperative) {
            if (isOffline) controls { refuseOffline() }
            else {
                val turn = current.cooperativeTurn()
                val next = current.nextTurnPlayerNumber
                if (turn != null && next != null) {
                    haptics.tick()
                    controls { passTo(turn, next) }
                } else passToTurn = turn
                playerMenu = null
                menuOpen = false
            }
            return
        }
        if (standalone && action in listOf("rematch", "sean-mode") && !current.settings.profile.deathMayDie) {
            pendingStart = seat to action; startCountdown(); return
        }
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
        if (readOnly) return
        unsent[unit.key] = (unsent[unit.key] ?: 0) + delta
        haptics.tick()
        gathering[unit.key]?.cancel()
        gathering[unit.key] = scope.launch {
            delay(GATHER_MS)
            flush(unit.key)
        }
    }

    val allUnits = tableUnits(summary)
    val soloOffered = !summary.settings.profile.cooperative
    // In one player's view a win claim's current confirmer joins it, so they can answer here.
    val selectedSolo = soloKey?.takeIf { soloOffered && allUnits.any { unit -> unit.key == it } }
    val selectedPass = passKeys.filter { key -> allUnits.any { it.key == key } }
    val units = gameCardUnits(allUnits, selectedSolo, selectedPass.takeIf { phoneView }.orEmpty(),
        summary.pending.winConfirmationPlayer)
    val solo = units.size < allUnits.size
    val turn = if (flipped) 180 else 0
    // Official modes keep their limits unless house rules are on (Atlas enforces the same).
    val limitsLocked = !summary.settings.houseRules &&
        com.turnhub.android.standalone.officialRules(summary.settings.profile, summary.settings.twoHeadedGiant) != null
    val observation = if (readOnly) Modifier
        .onPreviewKeyEvent { true }
        .clearAndSetSemantics {
            contentDescription = "${summary.state.name.lowercase()}. " +
                summary.players.joinToString(". ") { "${it.label}${it.life?.let { life -> ", life $life" } ?: ""}" }
        }
        .pointerInput(Unit) {
            awaitPointerEventScope {
                while (true) awaitPointerEvent(PointerEventPass.Initial).changes.forEach { it.consume() }
            }
        } else Modifier
    BoxWithConstraints(Modifier.fillMaxSize().then(observation)) {
        val landscape = maxWidth >= maxHeight || units.size <= 2
        val star = starLayout.on && summary.settings.profile.starCommander && units.size == StarCommander.PLAYERS
        // A cooperative table reads as one upright list, top to bottom, under a slim menu bar.
        val list = phoneView || summary.settings.profile.cooperative
        // Emperor: each team along one side, its emperor's bigger panel in the middle.
        val emperorTable = summary.emperor && !solo && units.size == com.turnhub.android.domain.Emperor.PLAYERS
        val plan = if (emperorTable) EMPEROR_PLACEMENT else placements(units.size)
        fun share(index: Int) = emperorWeight(emperorTable, index)
        val focused = units.indexOfFirst { unit -> unit.members.any { it.playerNumber == summary.pending.winConfirmationPlayer } }
            .takeIf { it >= 0 } ?: units.indexOfFirst { !it.out && it.members.any { member -> summary.hasTurn(member.playerNumber) } }
            .takeIf { it >= 0 } ?: units.indexOfFirst { !it.out }.coerceAtLeast(0)
        // The middle panel of a side with an odd count meets the centre disc at its inner (top) edge.
        val underHub = if (star || list || solo) emptySet()
            else listOf(plan.near, plan.far).filter { it.size % 2 == 1 }.map { it[it.size / 2] }.toSet()
        fun degreesOf(index: Int) = when {
            list || solo -> 0
            // A player's full-screen menu turns to the nearest side of their star seat.
            star -> Math.floorMod(Math.round(starSpot(index).degrees / 90f) * 90, 360)
            landscape -> if (index in plan.far) 180 else 0
            else -> if (index in plan.far) 90 else 270
        }
        fun openHub() {
            haptics.tick()
            if (attribution == null && bag?.pending == true) bagPrompt = bagPrompt.open()
            else menuOpen = true
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
                Box(modifier.padding(4.dp).then(
                    // List cards measure their own height; facing fixes both dimensions.
                    if (list) Modifier.rotate(turn.toFloat()) else Modifier.facing((degrees + turn) % 360),
                ), propagateMinConstraints = list) {
                    PlayerPanel(
                        unit = unit,
                        summary = summary,
                        life = life,
                        onPlanarRoll = planarRoll,
                        onOpenMenu = {
                            haptics.tick()
                            playerMenu = unit.key
                        },
                        onBump = { bump(unit, it) },
                        onControl = { seat, action -> send(seat, action) },
                        onTurnMark = { member, kind, value ->
                            haptics.tick()
                            controls { counter(member.seat(), kind, value, 1) }
                        },
                        onSkillLevel = { member, slot, level ->
                            controls { counter(member.seat(), StandaloneGame.SKILL_KIND, level, slot + 1) }
                        },
                        onEditSkills = { member, skills -> controls { investigatorSkills(member.seat(), skills) } },
                        onInvestigatorCard = { member, slot, card -> controls { investigatorCard(member.seat(), slot, card) } },
                        investigatorFocused = index == focused,
                        onRespond = { seat, id, accept ->
                            if (isOffline) controls { refuseOffline() }
                            else controls { this.respondLife(seat, id, accept) }
                        },
                        offlineCommander = tablet::offlineCommander,
                        haptics = haptics,
                        reduceMotion = reduceMotion,
                        underHub = index in underHub,
                    )
                }
            }
        }
        // The panels and centre disc, recorded so a finished game can be shared as a picture
        // (without the game-over card drawn over them).
        val tableShot = rememberGraphicsLayer()
        Box(Modifier.fillMaxSize().drawWithContent {
            tableShot.record { this@drawWithContent.drawContent() }
            drawLayer(tableShot)
        }) {
            if (list) {
                Column(Modifier.fillMaxSize().padding(4.dp)) {
                    HubBar(summary, Modifier.fillMaxWidth().padding(4.dp)) {
                        haptics.tick()
                        menuOpen = true
                    }
                    if (phoneView && soloOffered) {
                        DisclosureCard("Game card · " + (selectedSolo?.let { key -> allUnits.first { it.key == key }.name }
                            ?: "Pass and play")) {
                            Column(Modifier.heightIn(max = 220.dp).verticalScroll(rememberScrollState()),
                                verticalArrangement = Arrangement.spacedBy(6.dp)) {
                                Text("Choose one player's card, or follow the turn among selected players. Teams share a card.",
                                    color = p.muted, style = MaterialTheme.typography.bodySmall)
                                ToneButton(if (selectedSolo == null) "✓ Pass and play" else "Pass and play",
                                    { soloKey = null }, Modifier.fillMaxWidth())
                                if (selectedSolo == null) {
                                    Text("No selection shows everyone. Cards follow these players' turns.", color = p.muted,
                                        style = MaterialTheme.typography.bodySmall)
                                    allUnits.forEach { unit ->
                                        SetupSwitch(unit.name, "Include in pass and play", unit.key in selectedPass, true) { on ->
                                            if (on) passKeys.add(unit.key) else passKeys.remove(unit.key)
                                        }
                                    }
                                }
                                allUnits.forEach { unit ->
                                    ToneButton(if (selectedSolo == unit.key) "✓ Only ${unit.name}" else "Only ${unit.name}",
                                        { soloKey = unit.key }, Modifier.fillMaxWidth())
                                }
                            }
                        }
                    }
                    if (units.isNotEmpty()) {
                        val showOtherCards = !phoneView || summary.settings.profile.cooperative
                        key(units[focused].key) {
                            BoxWithConstraints(Modifier.weight(if (showOtherCards && units.size > 1) .70f else 1f).fillMaxWidth()) {
                                val cardHeight = maxHeight
                                Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState())) {
                                    panel(focused, 0, Modifier.fillMaxWidth().then(
                                        if (summary.settings.profile.deathMayDie) Modifier.heightIn(min = cardHeight)
                                        else Modifier.height(cardHeight.coerceAtLeast(300.dp))))
                                }
                            }
                        }
                        val others = units.indices.filter { showOtherCards && it != focused }
                        if (others.isNotEmpty()) Column(Modifier.weight(.30f).fillMaxWidth().verticalScroll(rememberScrollState())) {
                            others.forEach { panel(it, 0, Modifier.fillMaxWidth().then(
                                if (summary.settings.profile.deathMayDie) Modifier else Modifier.height(300.dp))) }
                        }
                    }
                }
            } else if (solo) {
                // One player's view: upright panels under the slim menu bar, so no centre disc
                // covers the life total.
                Column(Modifier.fillMaxSize().padding(4.dp)) {
                    HubBar(summary, Modifier.fillMaxWidth().padding(4.dp)) { openHub() }
                    units.indices.forEach { panel(it, 0, Modifier.weight(1f).fillMaxWidth()) }
                }
            } else if (star) {
                StarTable(panel)
            } else if (landscape) {
                Column(Modifier.fillMaxSize().padding(4.dp)) {
                    if (plan.far.isNotEmpty()) {
                        Row(Modifier.weight(1f).fillMaxWidth()) {
                            plan.far.forEach { panel(it, 180, Modifier.weight(share(it)).fillMaxHeight()) }
                        }
                    }
                    Row(Modifier.weight(1f).fillMaxWidth()) {
                        plan.near.forEach { panel(it, 0, Modifier.weight(share(it)).fillMaxHeight()) }
                    }
                }
            } else {
                // Portrait: the long sides become columns; each fills from the bottom
                // so turn order stays clockwise.
                Row(Modifier.fillMaxSize().padding(4.dp)) {
                    if (plan.far.isNotEmpty()) {
                        Column(Modifier.weight(1f).fillMaxHeight()) {
                            plan.far.asReversed().forEach { panel(it, 90, Modifier.weight(share(it)).fillMaxWidth()) }
                        }
                    }
                    Column(Modifier.weight(1f).fillMaxHeight()) {
                        plan.near.asReversed().forEach { panel(it, 270, Modifier.weight(share(it)).fillMaxWidth()) }
                    }
                }
            }

            if (!list && !solo) Hub(summary, Modifier.align(Alignment.Center)) { openHub() }
        }

        val menuIndex = units.indexOfFirst { it.key == playerMenu }
        if (menuIndex < 0 || summary.state == TableState.GAME_OVER) {
            if (playerMenu != null) playerMenu = null
        } else {
            val unit = units[menuIndex]
            Box(Modifier.fillMaxSize().facing((degreesOf(menuIndex) + turn) % 360)) {
                PlayerMenu(
                    unit = unit,
                    partners = partners,
                    summary = summary,
                    onClose = { playerMenu = null },
                    onControl = { seat, action ->
                        if (action == "concede" || action == "eliminate" || action == "win") playerMenu = null
                        // From one player's view a win waits on the others, like a guest device's claim.
                        send(seat, if (action == "win" && solo) "claim-win" else action)
                    },
                    onEliminate = { member, _ ->
                        if (isOffline) controls { refuseOffline() } else manualRemoval = member.playerNumber
                        playerMenu = null
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
                    onLife = { member, delta -> controls { life(member.seat(), delta) } },
                    onEditSkills = { member, skills -> controls { investigatorSkills(member.seat(), skills) } },
                    onInvestigatorCard = { member, slot, card -> controls { investigatorCard(member.seat(), slot, card) } },
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

        passToTurn?.takeIf { it == cooperativeTurn }?.let { turn ->
            CooperativePassDialog(summary, turn, onDismiss = { passToTurn = null }, onChoose = { target ->
                passToTurn = null
                if (current.cooperativeTurn() == turn && target in current.passPlayerNumbers) {
                    haptics.confirm()
                    controls { passTo(turn, target) }
                }
            })
        }

        val manualVictim = summary.players.firstOrNull { it.playerNumber == manualRemoval && !it.eliminated }
        val sourceVictim = if (attribution != null) attribution.takeIf { sourceOpen } else manualVictim
        if (!readOnly && sourceVictim != null) EliminationSourceDialog(summary, sourceVictim, attribution != null,
            enabled = !isOffline && !tablet.busy, onConfirm = { source ->
                val token = com.turnhub.android.protocol.GameActionToken(summary.bootId, summary.revision, bag?.cycle ?: 0, summary.game)
                if (attribution != null) {
                    val actor = summary.players.firstOrNull { !it.eliminated } ?: sourceVictim
                    controls { resolveElimination(actor.seat(), sourceVictim.playerNumber, source, token) }
                } else { controls { eliminate(sourceVictim.seat(), source) }; manualRemoval = null }
            }, onCancel = { if (attribution != null) sourceOpen = false else manualRemoval = null })
        if (bag != null && attribution == null && sourceVictim == null && showBag && !menuOpen && playerMenu == null &&
            summary.pending.eliminationTargetPlayer == null &&
            summary.state in listOf(TableState.RUNNING, TableState.PAUSED)) {
            DoomBagDialog(summary, !isOffline && !tablet.busy, onAction = { action, player, value ->
                val actor = summary.players.firstOrNull { !it.eliminated }?.seat()
                if (actor != null) {
                    bagPrompt = bagPrompt.open(); haptics.tick()
                    controls { doom(actor, action, com.turnhub.android.protocol.GameActionToken(summary.bootId, summary.revision, bag.cycle, summary.game), player, value) }
                }
            }, onResume = { dismissBag(); send(summary.players.firstOrNull { !it.eliminated }?.seat(), "pause") },
                onDismiss = { dismissBag() }, onOpenMenu = { dismissBag(); menuOpen = true })
        }
        val anySeat = summary.anySeat()
        val activeSeat = summary.players.firstOrNull { it.playerNumber == summary.activePlayerNumber }?.seat() ?: anySeat
        if (atlasRolling) PlanarRollDialog(reduceMotion, onRoll = {
            val face = com.turnhub.android.standalone.PlanarFace.roll(kotlin.random.Random.Default)
            send(activeSeat, "$PLANAR_PREFIX" + "roll:${face.wireValue}")
            face
        }, onFinish = { atlasRolling = false })
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
                    }
                }
            }
            TableState.GAME_OVER -> {
                // Every winner: one player, a Two-Headed Giant team, or Star Commander players sharing the win.
                val winners = allUnits.filter { u -> u.members.any { summary.isWinner(it.playerNumber) } }
                val names = winners.joinToString(" and ") { it.name }
                val star = summary.settings.profile.starCommander
                val together = summary.settings.profile.cooperative
                val emperorTeam = winners.firstOrNull()?.members?.first()?.playerNumber?.takeIf { summary.emperor }
                    ?.let { com.turnhub.android.domain.Emperor.teamName(com.turnhub.android.domain.Emperor.team(it - 1)) }
                Overlay(
                    when {
                        together -> if (winners.isEmpty()) "The investigators lost" else "The investigators win"
                        winners.isEmpty() -> "Game over"
                        star -> "Star Commander victory"
                        emperorTeam != null -> "Team $emperorTeam wins"
                        winners.size > 1 -> "Shared victory"
                        else -> "$names ${if (winners.single().members.size > 1) "win" else "wins"}"
                    },
                    when {
                        together -> summary.round?.let { "The game ended in round $it." }
                        winners.isEmpty() -> "The game ended as a draw."
                        emperorTeam != null -> "The other emperor is out. $names share the victory."
                        winners.size > 1 -> "$names share the victory."
                        star -> "$names wins. Well played, everyone."
                        else -> "Well played, everyone."
                    },
                ) {
                    if (winners.isNotEmpty()) WinnerRow(winners.flatMap { it.members })
                    if (shortGameUnsaved) {
                        Text("This game lasted under a minute, so it isn't in history or stats.", color = p.muted)
                        ToneButton("Count this game", onCountGame, Modifier.fillMaxWidth())
                    }
                    ToneButton("Share result", {
                        scope.launch {
                            val picture = tableShot.toImageBitmap().asAndroidBitmap()
                            if (onShareResult != null) onShareResult(picture)
                            else com.turnhub.android.ui.components.shareGameResult(context, picture,
                                summary.resultShareSummary(), p.bg.toArgb(), p.text.toArgb(), p.muted.toArgb())
                        }
                    }, Modifier.fillMaxWidth())
                    if (onShareCsv != null) ToneButton("Export CSV", onShareCsv, Modifier.fillMaxWidth())
                    OverlayButtons {
                        ToneButton("New players", { send(anySeat, "reset") }, Modifier.weight(1f))
                        AccentButton(if (standalone && summary.settings.profile.deathMayDie) "Set up rematch" else "Rematch", { send(anySeat, "rematch") }, Modifier.weight(1f))
                    }
                    // The Sean Mode easter egg: the same five, reseated so allies become enemies.
                    if (standalone && summary.star) {
                        ToneButton("Sean Mode: allies become enemies", { send(anySeat, "sean-mode") }, Modifier.fillMaxWidth())
                    }
                }
            }
            else -> Unit
        }
        if (summary.pokemonCheckup.isNotEmpty() && summary.state == TableState.RUNNING && !menuOpen && playerMenu == null) {
            Overlay("Pokémon Checkup", "Between turns. A card that changes these effects still applies.",
                onDismiss = { send(anySeat, "checkup-done") }) {
                PokemonCheckupLines(summary.pokemonCheckup)
            }
        }
        if (menuOpen && (summary.state == TableState.RUNNING || summary.state == TableState.PAUSED) &&
            summary.pending.winClaimPlayer == null && summary.pending.winConfirmationPlayer == null &&
            summary.pending.eliminationTargetPlayer == null) {
                Overlay("Game menu", null, onDismiss = { menuOpen = false }) {
                    BackToSetup(summary, enabled = !isOffline && anySeat != null) {
                        menuOpen = false; send(anySeat, "back-to-setup")
                    }
                    GameClockLine(summary)
                    if (attribution != null) AccentButton("Eliminated by? · ${attribution.label}", {
                        menuOpen = false; sourceOpen = true
                    }, Modifier.fillMaxWidth())
                    if (summary.doomBag != null) AccentButton("Bag of Doom · ${summary.players.count { !it.eliminated && summary.doomBag.contains(it.playerNumber) }} at risk", {
                        menuOpen = false; if (attribution != null) sourceOpen = true else bagPrompt = bagPrompt.open()
                    }, Modifier.fillMaxWidth())
                    if (summary.settings.profile.deathMayDie) DeathMayDieTurnPhasesButton()
                    val paused = summary.state == TableState.PAUSED
                    AccentButton(if (paused) "Resume game" else "Pause game", {
                        menuOpen = false; send(activeSeat, "pause")
                    }, Modifier.fillMaxWidth(), enabled = !isOffline && activeSeat != null)
                    summary.dayNight?.let { dayNight ->
                        DisclosureCard(com.turnhub.android.ui.components.dayNightLabel(dayNight)?.let { "Day & night · $it" } ?: "Day & night") {
                            com.turnhub.android.ui.components.DayNightControls(dayNight, !isOffline && anySeat != null) { action ->
                                send(anySeat, action)
                            }
                        }
                    }
                    // Atlas reverses turns in Magic formats (the ones it reports day/night for).
                    if ((standalone || summary.dayNight != null) && !summary.settings.profile.cooperative) {
                        val reversed = summary.turnOrderReversed
                        DisclosureCard(if (reversed) "Turn order · Reversed" else "Turn order") {
                            Text(if (reversed) "↺ Turns go counterclockwise until you set them back."
                                else "↻ Turns go clockwise. Reverse them when a card says so.", color = p.text, style = MaterialTheme.typography.bodyMedium)
                            ToneButton(if (reversed) "↻ Back to clockwise" else "↺ Reverse turn order", {
                                send(anySeat, if (reversed) "turn-order-clockwise" else "turn-order-reversed")
                            }, Modifier.fillMaxWidth(), tone = Tone.ACCENT, enabled = !isOffline && anySeat != null)
                        }
                    }
                    atlasPlanes?.let { planes ->
                        DisclosureCard("Planechase · ${planes.planeswalks} planeswalk${if (planes.planeswalks == 1) "" else "s"}") {
                            planes.plane?.let { card ->
                                Text((card.name ?: "A plane") + if (card.phenomenon) " (phenomenon)" else "", color = p.text,
                                    style = MaterialTheme.typography.titleMedium, fontWeight = FontWeight.SemiBold)
                                if (card.phenomenon) AccentButton("Resolve the phenomenon", { send(activeSeat, "${PLANAR_PREFIX}resolve") },
                                    Modifier.fillMaxWidth(), enabled = !isOffline && activeSeat != null)
                                Text("${planes.deckSize} cards in the planar deck.", color = p.muted, style = MaterialTheme.typography.bodySmall)
                            }
                            val last = planes.lastRoll?.let { roll ->
                                val who = summary.players.firstOrNull { it.playerNumber == roll.player }?.label ?: "Player ${roll.player}"
                                " Last roll: $who, ${roll.face}${if (roll.fromEffect) " (a card's roll)" else ""}."
                            }.orEmpty()
                            Text("Chaos ${planes.chaos} times. The next roll this turn costs ${planes.rollCost} mana.$last",
                                color = p.text, style = MaterialTheme.typography.bodyMedium)
                            Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                                ToneButton("Planeswalk", { send(activeSeat, "${PLANAR_PREFIX}planeswalk") }, Modifier.weight(1f),
                                    enabled = !isOffline && activeSeat != null)
                                ToneButton("Chaos ensues", { send(activeSeat, "${PLANAR_PREFIX}chaos") }, Modifier.weight(1f),
                                    enabled = !isOffline && activeSeat != null)
                            }
                            if (planes.digital) ToneButton("Top card to the bottom", { send(activeSeat, "${PLANAR_PREFIX}top-to-bottom") },
                                Modifier.fillMaxWidth(), enabled = !isOffline && activeSeat != null && planes.deckSize > 1)
                            Text("A card's own roll is free:", color = p.muted, style = MaterialTheme.typography.bodySmall)
                            ToneButton("Roll for a card", {
                                val face = com.turnhub.android.standalone.PlanarFace.roll(kotlin.random.Random.Default)
                                send(activeSeat, "${PLANAR_PREFIX}effect:${face.wireValue}")
                            }, Modifier.fillMaxWidth(), enabled = !isOffline && activeSeat != null)
                            Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically,
                                horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                                Text("Counters on the plane: ${planes.counters}", color = p.text,
                                    style = MaterialTheme.typography.titleSmall, modifier = Modifier.weight(1f))
                                ToneButton("−1", { send(anySeat, "${PLANAR_PREFIX}counter:-1") }, enabled = !isOffline && anySeat != null && planes.counters > 0)
                                ToneButton("+1", { send(anySeat, "${PLANAR_PREFIX}counter:1") }, tone = Tone.ACCENT, enabled = !isOffline && anySeat != null)
                            }
                        }
                    }
                    // A card that gives every player a counter: one tap instead of one per panel.
                    if ((standalone || summary.dayNight != null) && summary.settings.profile.magicRules) {
                        DisclosureCard("Everyone's counters") {
                            Text("Adds or removes one for every living player.", color = p.muted, style = MaterialTheme.typography.bodySmall)
                            EVERYONE_COUNTER_LABELS.forEach { (kind, label) ->
                                Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically,
                                    horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                                    Text(label, color = p.text, style = MaterialTheme.typography.titleSmall, modifier = Modifier.weight(1f))
                                    ToneButton("−1 all", { send(anySeat, "${StandaloneGame.EVERYONE_PREFIX}$kind:-1") },
                                        enabled = !isOffline && anySeat != null)
                                    ToneButton("+1 all", { haptics.tick(); send(anySeat, "${StandaloneGame.EVERYONE_PREFIX}$kind:1") },
                                        tone = Tone.ACCENT, enabled = !isOffline && anySeat != null)
                                }
                            }
                        }
                    }
                    DisclosureCard("Table tools") {
                        TableToolsCard(summary, reduceMotion) { haptics.tick() }
                    }
                    DisclosureCard("Game rules") {
                        ModeRulesButton(summary.settings.profile, summary.settings.twoHeadedGiant, Modifier.fillMaxWidth())
                        Text("Automatic elimination", color = p.muted, style = MaterialTheme.typography.titleSmall)
                        EliminationRulesEditor(summary.settings, !isOffline && anySeat != null, limitsLocked) { field, value ->
                            anySeat?.let { seat -> controls { setEliminationRule(seat, field, value) } }
                        }
                    }
                    if (summary.star) {
                        DisclosureCard("Seating & alliances") { StarMap(summary.starSeats()) }
                    }
                    DisclosureCard("Sound & display") {
                        com.turnhub.android.ui.components.GameSoundControls()
                        ToneButton("Turn panels around", { flipped = !flipped; menuOpen = false }, Modifier.fillMaxWidth())
                        if (soloOffered) {
                            Text("Show on this device", color = p.muted, style = MaterialTheme.typography.titleSmall)
                            ToneButton(if (soloKey == null) "✓ Whole table" else "Whole table", { soloKey = null; menuOpen = false },
                                Modifier.fillMaxWidth())
                            allUnits.forEach { unit ->
                                ToneButton(if (soloKey == unit.key) "✓ Only ${unit.name}" else "Only ${unit.name}",
                                    { soloKey = unit.key; menuOpen = false }, Modifier.fillMaxWidth())
                            }
                        }
                        if (summary.settings.profile.starCommander) {
                            SetupSwitch("Star layout", STAR_LAYOUT_HINT, starLayout.on, true, starLayout.set)
                        }
                    }
                    DisclosureCard("Help & app") {
                        com.turnhub.android.ui.components.ReportBugButton(
                            com.turnhub.android.ui.components.tableBugReportContext(summary, "Full-screen table"))
                        ToneButton("Back to the app", { menuOpen = false; onClose() }, Modifier.fillMaxWidth())
                    }
                    if (standalone) DisclosureCard("Finish game") {
                        ArmedButton(if (summary.settings.profile.cooperative) "The investigators lost" else "End without a winner",
                            "Tap again to end the game", Tone.BAD) {
                            menuOpen = false
                            send(anySeat, "draw")
                        }
                    }
                }
        }
    }
}

/** The winners' avatars and names on the game-over card. */
@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun WinnerRow(winners: List<TablePlayer>) {
    val p = palette
    FlowRow(horizontalArrangement = Arrangement.spacedBy(16.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
        winners.forEach { winner ->
            Column(horizontalAlignment = Alignment.CenterHorizontally) {
                com.turnhub.android.ui.components.PlayerAvatar(winner.label, winner.playerNumber, winner.avatar, size = 52.dp)
                Text(winner.label, color = p.text, fontWeight = FontWeight.SemiBold, maxLines = 1, overflow = TextOverflow.Ellipsis,
                    modifier = Modifier.widthIn(max = 120.dp))
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
        Text(if (summary.state == TableState.PAUSED) "Paused" else clock, color = p.text,
            style = MaterialTheme.typography.titleMedium, fontWeight = FontWeight.SemiBold)
        Text(if (summary.players.any { it.eliminationPending }) "Eliminated by?" else if (summary.doomBag?.pending == true) "Bag check due" else summary.round?.let { "Round $it" } ?: com.turnhub.android.ui.components.dayNightLabel(summary.dayNight) ?: "Game menu", color = p.muted, style = MaterialTheme.typography.labelSmall)
    }
}

/** The list layout's menu bar: the round, the game clock, and the table menu on tap. */
@Composable
private fun HubBar(summary: TableSummary, modifier: Modifier, onOpen: () -> Unit) {
    val p = palette
    val nowMs = rememberNowMs(ticking = summary.state == TableState.RUNNING, intervalMs = 500)
    val clock = TableClock.format(TableClock.gameElapsedMs(summary, nowMs))
    Row(
        modifier
            .clip(RoundedCornerShape(99.dp))
            .background(p.surface)
            .border(2.dp, p.lineStrong, RoundedCornerShape(99.dp))
            .clickable(onClickLabel = "Open the table menu", role = Role.Button, onClick = onOpen)
            .padding(horizontal = 16.dp, vertical = 8.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        summary.round?.let { Text("Round $it", color = p.text, style = MaterialTheme.typography.titleMedium, fontWeight = FontWeight.SemiBold) }
        Text(if (summary.state == TableState.PAUSED) "Paused" else clock, color = p.muted, style = MaterialTheme.typography.titleSmall)
        // Death May Die: where the Elder One stands, once the table tracks it.
        summary.elder?.takeIf { it.inUse }?.let { elder ->
            Text(if (elder.summoned) com.turnhub.android.domain.ElderOneTrack.stageName(elder.stage)
                else "Track ${elder.space}/${com.turnhub.android.domain.ElderOneTrack.LAST_SPACE}",
                color = p.muted, style = MaterialTheme.typography.titleSmall, maxLines = 1)
        }
        com.turnhub.android.ui.components.dayNightLabel(summary.dayNight)?.let {
            Text(it, color = p.muted, style = MaterialTheme.typography.titleSmall, maxLines = 1)
        }
        if (summary.turnOrderReversed) Text("↺ Reversed", color = p.warn, style = MaterialTheme.typography.titleSmall, maxLines = 1)
        Spacer(Modifier.weight(1f))
        Text("Game menu", color = p.text, style = MaterialTheme.typography.labelLarge)
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
    BoxWithConstraints(
        Modifier
            .fillMaxSize()
            .background(p.scrim)
            // Swallows touches so nothing under the card changes; a tap outside closes the menu.
            .pointerInput(onDismiss) { detectTapGestures { onDismiss?.invoke() } },
        contentAlignment = Alignment.Center,
    ) {
        BrassCard(Modifier.padding(12.dp).widthIn(max = 440.dp).heightIn(max = (maxHeight - 24.dp).coerceAtLeast(0.dp))
            .pointerInput(Unit) { detectTapGestures { } }) {
            Text(title, color = p.text, style = MaterialTheme.typography.headlineSmall)
            text?.let { Text(it, color = p.muted) }
            Column(Modifier.weight(1f, fill = false).verticalScroll(rememberScrollState()),
                verticalArrangement = Arrangement.spacedBy(8.dp)) { content() }
            onDismiss?.let { ToneButton("Done", it, Modifier.fillMaxWidth()) }
        }
    }
}

@Composable
private fun GameClockLine(summary: TableSummary) {
    val nowMs = rememberNowMs(ticking = summary.state == TableState.RUNNING, intervalMs = 500)
    Text("Game clock ${TableClock.format(TableClock.gameElapsedMs(summary, nowMs))}", color = palette.muted)
}

/** Undoes the start in the first minutes (a wrong setup): back to the lobby, nothing recorded. */
@Composable
private fun BackToSetup(summary: TableSummary, enabled: Boolean = true, onConfirm: () -> Unit) {
    val nowMs = rememberNowMs(ticking = summary.state == TableState.RUNNING, intervalMs = 1_000)
    val left = com.turnhub.android.standalone.StandaloneGame.UNDO_START_MS - TableClock.gameElapsedMs(summary, nowMs)
    if (left <= 0) return
    val label = "Back to setup (${TableClock.format(left)} left)"
    if (enabled) ArmedButton(label, "Tap again: this game won't be saved", Tone.WARN, onConfirm)
    else ToneButton(label, {}, Modifier.fillMaxWidth(), tone = Tone.WARN, enabled = false)
    Text("Returns to setup with the same players and settings. Available for the first three minutes.",
        color = palette.muted, style = MaterialTheme.typography.bodySmall)
}

@Composable
internal fun PlayerPanel(
    unit: TableUnit,
    summary: TableSummary,
    life: Int,
    onOpenMenu: () -> Unit,
    onPlanarRoll: (() -> Unit)?,
    onBump: (Int) -> Unit,
    onControl: (TabletSeat, String) -> Unit,
    /** Pokémon and Lorcana: marks a once-per-turn action or Special Condition (see [TurnPanelLines]). */
    onTurnMark: (TablePlayer, String, Int) -> Unit,
    onSkillLevel: (TablePlayer, Int, Int) -> Unit,
    onEditSkills: (TablePlayer, List<com.turnhub.android.domain.InvestigatorSkill>) -> Unit,
    onInvestigatorCard: (TablePlayer, InvestigatorCardSlot, InvestigatorCard?) -> Unit,
    investigatorFocused: Boolean,
    onRespond: (TabletSeat, Long, Boolean) -> Unit,
    offlineCommander: (Long, Int, Int) -> Int,
    haptics: TurnHubHaptics,
    reduceMotion: Boolean = false,
    underHub: Boolean = false,
    controlsEnabled: Boolean = true,
) {
    val p = palette
    val tracks = summary.settings.profile.deathMayDie
    val investigatorArt = rememberInvestigatorArt(unit.members.first().investigator.takeIf { tracks })
    val investigatorCard = if (tracks) rememberCommanderArt(unit.members.first().commander, unit.members.first().commanderArt) else null
    val dashboardArt = investigatorArt ?: investigatorCard
    val shape = RoundedCornerShape(22.dp)
    val hue = unit.members.first().color?.let { Color(android.graphics.Color.parseColor(it)) }
        ?: p.avatarColor(unit.members.first().playerNumber)
    val active = unit.members.any { summary.hasTurn(it.playerNumber) } && summary.state != TableState.GAME_OVER
    val out = unit.out
    val recentDelta = rememberRecentLifeChange(life, summary.state == TableState.RUNNING || summary.state == TableState.PAUSED)
    val (small, big) = lifeSteps(summary.settings.profile)
    val name = unit.name
    // The commanders a team or player named (Death May Die: their investigator), shown under the name.
    val commanders = unit.members.mapNotNull { if (summary.settings.profile.deathMayDie) it.investigator else it.namedCommander }
        .takeIf { it.isNotEmpty() }?.joinToString(" & ")
    // The active panel's edge breathes slowly; steady when motion is reduced. The value is
    // read only while drawing, so the pulse redraws the edge without recomposing the panel.
    val glow = if (active && !reduceMotion) {
        rememberInfiniteTransition(label = "turnGlow").animateFloat(
            .55f, 1f, infiniteRepeatable(tween(1_100), RepeatMode.Reverse), label = "glow",
        )
    } else null
    // An emperor's turn edge is gold (owner, 2026-10-21); generals keep the usual colour.
    val emperorSeat = (unit.members.first().playerNumber - 1).takeIf { summary.emperor }
    val activeColor = if (emperorSeat != null && com.turnhub.android.domain.Emperor.isEmperor(emperorSeat)) p.emperorGold else p.active
    BoxWithConstraints(
        Modifier
            .then(if (tracks) Modifier.fillMaxWidth() else Modifier.fillMaxSize())
            .clip(shape)
            .background(p.surface)
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
        // Team panels share space; each player's chosen background gets an equal share.
        Row(Modifier.matchParentSize()) {
            unit.members.forEach { member ->
                // Death May Die: the investigator's picture, when there is one, before any tile card.
                val card = if (tracks) dashboardArt else rememberCommanderArt(member.commander, member.commanderArt)
                PlayerTileBackground(member.tileBackground,
                    com.turnhub.android.ui.components.playerColor(member.color, p.avatarColor(member.playerNumber)),
                    Modifier.weight(1f).fillMaxHeight(), art = card, showCredit = !tracks)
            }
        }
        if (tracks) {
            if (investigatorFocused) InvestigatorPanelContent(unit, summary, life, onBump, onTurnMark, onOpenMenu,
                onControl, onRespond, onSkillLevel, onEditSkills, onInvestigatorCard, dashboardArt)
            else CompactInvestigatorPanel(unit, summary, life, onOpenMenu, onTurnMark, dashboardArt)
            return@BoxWithConstraints
        }
        val nameMax = maxWidth * .6f
        val panelWidth = maxWidth
        // Short or narrow panels put the clock inside Pass so it keeps a usable target.
        val compact = maxHeight < 260.dp || maxWidth < 420.dp
        val profile = summary.settings.profile
        // Pokémon and Lorcana: the number is prizes or lore, with turn reminders under it.
        val cardGame = !profile.countsLife
        val turnHeight = if (cardGame) turnPanelHeight(unit, summary, maxWidth < 420.dp) else 0f
        // Turn lines keep their room on every panel, so the number stays put when the turn passes.
        val turnRoom = if (cardGame) maxOf(turnHeight, turnPanelReserve(unit, summary, maxWidth < 420.dp)) else 0f
        val planarCompact = compact && active && !out && summary.state == TableState.RUNNING && onPlanarRoll != null && maxWidth < 420.dp
        // Column measures the actual action bar first. Life gestures only occupy the
        // remaining area; gaps around Pass can never fall through to life controls.
        Column(Modifier.fillMaxSize()) {
            BoxWithConstraints(Modifier.weight(1f).fillMaxWidth().clipToBounds()) {
                val between = (maxHeight.value - turnRoom - 52f).coerceAtLeast(24f)
                // Room for the lines under the number, so a short panel shrinks the number instead of
                // squashing those lines to slivers.
                val demerits = unit.members.first().demerits
                val linesBelow = (if (demerits > 0) 22f else 0f) + (if (summary.star || summary.emperor) 22f else 0f) + (if (cardGame) 20f else 0f) +
                    (if (summary.settings.profile.magicRules) unit.members.count { m -> counterChips(m.counters, summary.initiativePlayerNumber == m.playerNumber,
                        summary.settings.profile.commanderRules, summary.settings.poisonLimit).isNotEmpty() } * 30f else 0f) +
                    (if (summary.settings.profile.commanderRules && unit.members.any { m -> m.commanderDamage.any { d -> d.damage.any { it > 0 } } }) 26f else 0f)
                // The room those lines may need is kept from the start of the game, so the first counter or
                // commander damage doesn't shrink or lift the number; only more than that grows the band.
                val linesRoom = maxOf(linesBelow, (if (summary.star || summary.emperor) 22f else 0f) + (if (cardGame) 20f else 0f) +
                    (if (summary.settings.profile.magicRules) 30f else 0f) + (if (summary.settings.profile.commanderRules) 26f else 0f))
                val lifeSize = minOf(
                    minOf(maxWidth, maxHeight).value * if ("$life".length > 3) .22f else .36f,
                    (between - linesRoom) * .8f,
                ).coerceIn(24f, 160f)
                Row(Modifier.fillMaxSize().alpha(if (out) .45f else 1f)) {
                    HoldHalf("−", stepLabel(profile, name, small, adding = false), !out && controlsEnabled,
                        { onBump(-small) }, { onBump(-big) }, Alignment.CenterStart, Modifier.weight(1f).fillMaxHeight())
                    HoldHalf("+", stepLabel(profile, name, small, adding = true), !out && controlsEnabled,
                        { onBump(small) }, { onBump(big) }, Alignment.CenterEnd, Modifier.weight(1f).fillMaxHeight())
                }

                Row(
                    Modifier.align(Alignment.TopStart).padding(horizontal = 16.dp, vertical = 12.dp)
                        // Stops short of the centre disc, which covers the middle of this edge.
                        .then(if (underHub) Modifier.widthIn(max = (panelWidth / 2 - 62.dp).coerceAtLeast(64.dp)) else Modifier),
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(8.dp),
                ) {
                    unit.members.forEach { member -> member.avatar?.let {
                        com.turnhub.android.ui.components.AvatarGlyph(it, p.text, 32.dp)
                    } }
                    Column(Modifier.weight(1f, fill = false).widthIn(max = nameMax)) {
                        Text(name, color = p.text, style = MaterialTheme.typography.titleMedium, fontWeight = FontWeight.SemiBold,
                            maxLines = 1, overflow = TextOverflow.Ellipsis)
                        commanders?.let {
                            Text(it, color = p.muted, style = MaterialTheme.typography.labelMedium, fontStyle = FontStyle.Italic,
                                maxLines = 1, overflow = TextOverflow.Ellipsis)
                        }
                    }
                    if (summary.doomBag != null && summary.players.count { !it.eliminated } > 2 && !out) {
                        val risk = summary.doomBag.contains(unit.members.first().playerNumber)
                        Tag(if (risk) "In bag" else "Safe", if (risk) p.warn else p.good, p.surface)
                    }
                    emperorSeat?.let { seat ->
                        Tag(emperorTag(seat), p.emperorTeamColor(com.turnhub.android.domain.Emperor.team(seat)), p.surface)
                    }
                    if (active) Tag("Turn", activeColor, p.onActive)
                    if (out) Tag(if (unit.members.any { it.conceded }) "Conceded" else if (unit.members.any { it.selfEliminated }) "Eliminated by self" else "Eliminated", p.bad, p.surface)
                    unit.members.first().eliminatedBy?.let { source ->
                        summary.players.firstOrNull { it.playerNumber == source }?.let { Text("By ${it.label}", color = p.muted) }
                    }
                }

                unit.members.firstOrNull { it.playerNumber == summary.monarchPlayerNumber }?.let { monarch ->
                    MonarchBadge(if (unit.members.size > 1) monarch.label else null,
                        Modifier.align(Alignment.TopStart).padding(start = 16.dp, top = if (commanders != null) 70.dp else 52.dp))
                }

                Column(
                    Modifier.align(Alignment.Center).padding(top = 44.dp, bottom = 8.dp + turnRoom.dp)
                        .alpha(if (out) .45f else 1f)
                        .clearAndSetSemantics {
                            contentDescription = scoreDescription(profile, name, life, summary.settings.startingLife)
                            liveRegion = LiveRegionMode.Polite
                        },
                    horizontalAlignment = Alignment.CenterHorizontally,
                ) {
                    // The running change floats as a pill over the number's top edge, so it never moves it.
                    val pill = if (recentDelta == 0) null else (if (recentDelta > 0) "+" else "−") + kotlin.math.abs(recentDelta)
                    Box(contentAlignment = Alignment.Center) {
                        LifeNumber(life, lifeSize, reduceMotion)
                        if (pill != null) {
                            val tone = if (recentDelta > 0) p.good else p.bad
                            Text(pill, color = tone, style = MaterialTheme.typography.titleMedium, fontWeight = FontWeight.ExtraBold,
                                modifier = Modifier
                                    .align(Alignment.TopEnd)
                                    .offset(x = 18.dp, y = (-10).dp)
                                    .background(p.surface.copy(alpha = .9f), RoundedCornerShape(99.dp))
                                    .border(1.dp, tone.copy(alpha = .6f), RoundedCornerShape(99.dp))
                                    .padding(horizontal = 10.dp, vertical = 1.dp))
                        }
                    }
                    // A fixed band under the number: lines appearing in it never move the number.
                    Column(Modifier.height(linesRoom.dp).wrapContentHeight(Alignment.Top, unbounded = true),
                        horizontalAlignment = Alignment.CenterHorizontally) {
                        scoreCaption(profile, life, summary.settings.startingLife)?.let {
                            Text(it, color = p.muted, style = MaterialTheme.typography.labelLarge)
                        }
                        CommanderSummary(unit, summary, offlineCommander)
                        if (demerits > 0) Text("Demerits · $demerits", color = p.text, style = MaterialTheme.typography.labelMedium)
                        StarProgressLine(unit, summary, panelWidth)
                        EmperorRangeLine(unit, summary, panelWidth)
                        if (summary.settings.profile.magicRules) {
                            unit.members.forEach { member ->
                                CounterChips(member.counters, summary.initiativePlayerNumber == member.playerNumber,
                                    summary.settings.profile.commanderRules, summary.settings.poisonLimit,
                                    Modifier.padding(top = 4.dp), compact = true, commanderRarity = rememberCommanderArt(member.namedCommander, member.commanderArt)?.rarity)
                            }
                        }
                    }
                }

                if (cardGame && !out) {
                    TurnPanelLines(unit, summary, onTurnMark,
                        Modifier.align(Alignment.BottomCenter).fillMaxWidth().padding(horizontal = 12.dp)
                            .padding(bottom = 8.dp), enabled = controlsEnabled)
                }
            }
            Column(Modifier.fillMaxWidth().padding(top = 12.dp)
                .background(p.surface.copy(alpha = .94f)).padding(horizontal = 10.dp, vertical = 8.dp),
                verticalArrangement = Arrangement.spacedBy(4.dp)) {
                if (planarCompact) {
                    Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                        MenuButton(name, true, onOpenMenu)
                        Spacer(Modifier.weight(1f))
                        TurnClock(summary)
                    }
                    Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                        val actor = unit.members.firstOrNull { it.playerNumber == summary.activePlayerNumber }
                        if (actor != null) HoldToPass(summary.pending.passPlayer != null,
                            { onControl(actor.seat(), "pass") }, haptics, Modifier.weight(1f), true)
                        PlanarDieButton(onPlanarRoll!!, true, Modifier.weight(1f))
                    }
                } else {
                    // As tall as Pass on every panel, so the bar doesn't grow when this panel's turn starts.
                    Row(
                        Modifier.fillMaxWidth().heightIn(min = 64.dp),
                        verticalAlignment = Alignment.CenterVertically,
                        horizontalArrangement = Arrangement.spacedBy(10.dp),
                    ) {
                        if (controlsEnabled) MenuButton(name, compact, onOpenMenu)
                        else Text(name, color = p.muted, style = MaterialTheme.typography.labelLarge)
                        val actor = unit.members.firstOrNull { it.playerNumber == summary.activePlayerNumber }
                        val canPass = controlsEnabled && actor != null && !out && summary.state == TableState.RUNNING
                        if (canPass && actor != null) {
                            val passing = summary.pending.passPlayer != null && unit.members.any { it.playerNumber == summary.pending.passPlayer }
                            HoldToPass(passing, { onControl(actor.seat(), "pass") }, haptics,
                                Modifier.weight(1f), compact) { foreground -> if (compact) TurnClock(summary, foreground) }
                        }
                        if (canPass && onPlanarRoll != null) {
                            PlanarDieButton(onPlanarRoll, compact)
                        }
                        if (compact && active && !canPass) TurnClock(summary)
                        if (!canPass) Spacer(Modifier.weight(1f))
                        if (active && !compact) TurnClock(summary)
                    }
                }
            }
        }

        if (controlsEnabled) Prompt(unit, summary, onControl, onRespond, Modifier.align(Alignment.BottomCenter))
        // A win claim or life request comes first; the rad reminder waits behind it.
        val asking = summary.pending.winConfirmationPlayer != null || unit.members.any { it.lifeRequest?.state == LifeRequestState.PENDING }
        if (controlsEnabled && !asking) unit.members.firstOrNull { it.playerNumber in summary.radDuePlayerNumbers }?.let { member ->
            RadPrompt(member, unit.members.size > 1, { nonlands -> onTurnMark(member, StandaloneGame.RAD_KIND, nonlands) },
                Modifier.align(Alignment.BottomCenter))
        }
    }
}

/** A cooperative card grows with its counters and text, including wrapped rows and large type. */
@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun InvestigatorPanelContent(
    unit: TableUnit,
    summary: TableSummary,
    wounds: Int,
    onWound: (Int) -> Unit,
    onMark: (TablePlayer, String, Int) -> Unit,
    onOpenMenu: () -> Unit,
    onControl: (TabletSeat, String) -> Unit,
    onRespond: (TabletSeat, Long, Boolean) -> Unit,
    onSkillLevel: (TablePlayer, Int, Int) -> Unit,
    onEditSkills: (TablePlayer, List<com.turnhub.android.domain.InvestigatorSkill>) -> Unit,
    onInvestigatorCard: (TablePlayer, InvestigatorCardSlot, InvestigatorCard?) -> Unit,
    art: TileArt?,
) {
    val p = palette
    val member = unit.members.first()
    val active = summary.hasTurn(member.playerNumber) && summary.state != TableState.GAME_OVER
    val details: @Composable () -> Unit = {
      Column(Modifier.fillMaxWidth(), verticalArrangement = Arrangement.spacedBy(12.dp)) {
        Column(Modifier.fillMaxWidth()) {
            member.avatar?.let { com.turnhub.android.ui.components.AvatarGlyph(it, p.text, 32.dp) }
            Text(member.label, color = p.text, style = MaterialTheme.typography.titleMedium, fontWeight = FontWeight.SemiBold)
            member.investigator?.let {
                Text(it, color = p.muted, style = MaterialTheme.typography.labelMedium, fontStyle = FontStyle.Italic)
            }
            if (active) Tag("Turn", p.active, p.onActive)
            if (unit.out) Tag(if (member.conceded) "Conceded" else "Eliminated", p.bad, p.surface)
        }
        InvestigatorTracks(unit, summary, wounds, onWound, onMark,
            Modifier.fillMaxWidth().alpha(if (unit.out) .45f else 1f)) { who, action -> onControl(who.seat(), action) }
        FlowRow(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(10.dp),
            verticalArrangement = Arrangement.spacedBy(8.dp), itemVerticalAlignment = Alignment.CenterVertically) {
            MenuButton(member.label, true, onOpenMenu)
            if (active && !unit.out) {
                TurnClock(summary)
                if (summary.state == TableState.RUNNING) {
                    ToneButton("Pass", { onControl(member.seat(), "pass") })
                }
            }
        }
        DeathMayDieTurnPhasesButton()
        InvestigatorArtCredit(art)
        Prompt(unit, summary, onControl, onRespond, Modifier)
      }
    }
    val abilities: @Composable () -> Unit = {
        InvestigatorAbilities(member, summary,
            onInsanity = { onMark(member, StandaloneGame.INSANITY_KIND, it) },
            onLevel = { slot, level -> onSkillLevel(member, slot, level) },
            onEdit = { onEditSkills(member, it) }, onCard = { slot, card -> onInvestigatorCard(member, slot, card) })
    }
    BoxWithConstraints(Modifier.fillMaxWidth().padding(16.dp)) {
        if (maxWidth >= 600.dp) Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(16.dp)) {
            Column(Modifier.weight(.38f)) { details() }
            Column(Modifier.weight(.62f)) { abilities() }
        } else Column(Modifier.fillMaxWidth(), verticalArrangement = Arrangement.spacedBy(12.dp)) {
            details()
            abilities()
        }
    }
}

@Composable
private fun CompactInvestigatorPanel(unit: TableUnit, summary: TableSummary, wounds: Int,
    onOpenMenu: () -> Unit, onMark: (TablePlayer, String, Int) -> Unit, art: TileArt?) {
    val member = unit.members.first()
    Column(Modifier.fillMaxWidth().padding(12.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
        Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(10.dp)) {
            Column(Modifier.weight(1f)) {
                Text(member.label, color = palette.text, fontWeight = FontWeight.SemiBold)
                member.investigator?.let { Text(it, color = palette.muted, style = MaterialTheme.typography.labelMedium) }
                if (unit.out) Text(if (member.conceded) "Conceded" else "Eliminated", color = palette.bad,
                    style = MaterialTheme.typography.labelMedium)
                Text("Wounds $wounds · Stress ${member.stress} · Insanity ${member.dashboard.insanity}",
                    color = palette.text, style = MaterialTheme.typography.labelMedium)
            }
            MenuButton(member.label, true, onOpenMenu)
        }
        InvestigatorArtCredit(art)
        TurnPanelLines(unit, summary, onMark, Modifier.fillMaxWidth())
    }
}

@Composable
private fun InvestigatorArtCredit(art: TileArt?) {
    if (art == null) return
    Text("Art: ${art.artist}", color = palette.muted, style = MaterialTheme.typography.labelSmall,
        modifier = Modifier.semantics { contentDescription = "${art.cardName} art by ${art.artist}, from ${art.source}" })
}

@Composable
private fun PlanarDieButton(onRoll: () -> Unit, compact: Boolean, modifier: Modifier = Modifier) {
    val p = palette
    val shape = RoundedCornerShape(14.dp)
    Box(modifier.heightIn(min = 48.dp).clip(shape).background(p.activeSoft).border(1.dp, p.active, shape)
        .clickable(onClickLabel = "Roll the planar die", role = Role.Button, onClick = onRoll)
        .padding(horizontal = 8.dp, vertical = if (compact) 6.dp else 12.dp), contentAlignment = Alignment.Center) {
        Text("Planar die", color = p.text, fontWeight = FontWeight.SemiBold, maxLines = 1,
            style = if (compact) MaterialTheme.typography.labelLarge else LocalTextStyle.current)
    }
}

/** The life total, counting to its new value; only this text recomposes while it animates. */
/**
 * The life total's running change, like "+1" or "−3", from whatever moved it: this device's
 * taps, a joined phone or the host. It adds up changes that come close together and clears
 * [RECENT_LIFE_MS] after the last one. Only during a match, so a new game's reset never shows.
 */
@Composable
private fun rememberRecentLifeChange(life: Int, live: Boolean): Int {
    var baseline by remember(live) { mutableStateOf(life) }
    var recent by remember(live) { mutableStateOf(0) }
    LaunchedEffect(life, live) {
        if (!live) { baseline = life; recent = 0; return@LaunchedEffect }
        if (life == baseline) return@LaunchedEffect
        recent += life - baseline
        baseline = life
        delay(RECENT_LIFE_MS)
        recent = 0
    }
    return recent
}

private const val RECENT_LIFE_MS = 3_000L

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
            .clickable(onClickLabel = "More for $name", role = Role.Button, onClick = onOpen)
            .padding(horizontal = if (compact) 14.dp else 16.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(6.dp),
    ) {
        Text("☰", color = p.text, fontSize = 18.sp)
        // A short panel keeps just the icon; its spoken label still says Menu.
        if (!compact) Text("More", color = p.text, fontWeight = FontWeight.SemiBold)
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

/** Cancels when a life gesture leaves its actual bounds, including platform touch slop. */
private suspend fun AwaitPointerEventScope.waitForLifeRelease(): PointerInputChange? {
    while (true) {
        val event = awaitPointerEvent(PointerEventPass.Main)
        if (event.changes.any { change -> change.isConsumed ||
            change.position.x < 0 || change.position.x >= size.width ||
            change.position.y < 0 || change.position.y >= size.height }) return null
        if (event.changes.all { !it.pressed }) return event.changes.first()
        if (awaitPointerEvent(PointerEventPass.Final).changes.any { it.isConsumed }) return null
    }
}

/** Half the life area: tap for one step, hold to repeat the big step. */
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
                    val down = awaitFirstDown()
                    // Compose can expand small targets; it must not expand life into the action strip.
                    if (down.position.x < 0 || down.position.x >= size.width ||
                        down.position.y < 0 || down.position.y >= size.height) return@awaitEachGesture
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
                    try {
                        val up = waitForLifeRelease()
                        if (up != null && !held) {
                            up.consume()
                            step()
                        }
                    } finally {
                        repeating.cancel()
                        pressed = false
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
private fun HoldToPass(passing: Boolean, onPass: () -> Unit, haptics: TurnHubHaptics, modifier: Modifier, compact: Boolean = false,
    clock: @Composable (Color?) -> Unit = {}) {
    val p = palette
    val pass by rememberUpdatedState(onPass)
    val scope = rememberCoroutineScope()
    var holding by remember { mutableStateOf(false) }
    val shape = RoundedCornerShape(14.dp)
    val label = if (passing) "Passing… tap to keep" else if (compact) "Pass" else "Hold to pass"
    Box(
        modifier
            .clip(shape)
            .background(if (holding || passing) p.active else p.activeSoft)
            .border(1.dp, p.active, shape)
            .semantics(mergeDescendants = true) {
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
                    try {
                        waitForUpOrCancellation()
                    } finally {
                        timer.cancel()
                        holding = false
                    }
                }
            }
            .heightIn(min = 64.dp)
            .padding(horizontal = 8.dp, vertical = if (compact) 6.dp else 12.dp),
        contentAlignment = Alignment.Center,
    ) {
        Column(horizontalAlignment = Alignment.CenterHorizontally, verticalArrangement = Arrangement.spacedBy(2.dp)) {
            Text(label, color = if (holding || passing) p.onActive else p.text, fontWeight = FontWeight.SemiBold,
                maxLines = 2, overflow = TextOverflow.Ellipsis, textAlign = TextAlign.Center,
                style = if (compact) MaterialTheme.typography.labelLarge else LocalTextStyle.current)
            clock(if (holding || passing) p.onActive else null)
        }
    }
}

@Composable
private fun TurnClock(summary: TableSummary, foreground: Color? = null) {
    val p = palette
    val nowMs = rememberNowMs(ticking = summary.state == TableState.RUNNING, intervalMs = 500)
    val expired = summary.turnTimer.phase == TurnTimerPhase.EXPIRED
    val remaining = TableClock.turnRemainingMs(summary, nowMs)
    // Over time shows "+" and the overtime, so time-up never rests on color alone.
    val text = if (expired) "+" + TableClock.format(TableClock.overtimeMs(summary, nowMs))
        else TableClock.format(remaining ?: TableClock.turnElapsedMs(summary, nowMs))
    val color = foreground ?: when (summary.turnTimer.phase) {
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

/** Star Commander: "Enemies out 1/2", with the enemies' names when the panel is wide enough. */
@Composable
private fun StarProgressLine(unit: TableUnit, summary: TableSummary, width: androidx.compose.ui.unit.Dp) {
    if (!summary.star) return
    val member = unit.members.first()
    val seats = summary.starSeats()
    val seat = member.playerNumber - 1
    val enemies = summary.starEnemies(member.playerNumber)
    val names = enemies.joinToString(", ") { it.label + if (it.eliminated) " (out)" else "" }
    Text(
        starProgress(seats, seat) + if (width >= 260.dp) " · $names" else "",
        color = palette.muted,
        style = MaterialTheme.typography.labelMedium,
        maxLines = 1,
        overflow = TextOverflow.Ellipsis,
        modifier = Modifier.padding(top = 2.dp),
    )
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun CommanderSummary(unit: TableUnit, summary: TableSummary, offlineCommander: (Long, Int, Int) -> Int) {
    val p = palette
    if (!summary.settings.profile.commanderRules) return
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
 * Rad counters trigger at the start of the player's turn (CR 727): they mill that many cards,
 * then say how many weren't lands; the host takes that much life and rad (owner, 2026-10-20).
 */
@Composable
private fun RadPrompt(member: TablePlayer, named: Boolean, onResolve: (Int) -> Unit, modifier: Modifier) {
    val p = palette
    val rad = member.counters.rad
    if (rad <= 0) return
    var nonlands by remember(rad) { mutableStateOf(0) }
    BrassCard(modifier.padding(10.dp).widthIn(max = 420.dp).pointerInput(Unit) { detectTapGestures { } }, highlight = p.warn) {
        Text("Resolve rad" + if (named) " · ${member.label}" else "", color = p.warn, style = MaterialTheme.typography.titleMedium,
            fontWeight = FontWeight.Bold, modifier = Modifier.semantics { liveRegion = LiveRegionMode.Assertive })
        Text("Mill $rad. How many nonlands?", color = p.text)
        Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(10.dp)) {
            ToneButton("−", { nonlands-- }, enabled = nonlands > 0)
            Text("$nonlands", color = p.text, style = MaterialTheme.typography.titleLarge, fontWeight = FontWeight.Bold,
                modifier = Modifier.semantics { contentDescription = "$nonlands nonlands" })
            ToneButton("+", { nonlands++ }, enabled = nonlands < rad)
            AccentButton(if (nonlands == 0) "None" else "Lose $nonlands", { onResolve(nonlands) }, Modifier.weight(1f))
        }
    }
}

/**
 * A player's menu over the whole table, turned to face that player: counters,
 * Commander damage received, claiming the win and conceding. It closes with
 * the ✕, the Done button at the bottom, or Back.
 */
@Composable
internal fun PlayerMenu(
    unit: TableUnit,
    partners: MutableList<Int>,
    summary: TableSummary,
    onClose: () -> Unit,
    onControl: (TabletSeat, String) -> Unit,
    onEliminate: (TablePlayer, Int?) -> Unit,
    onCommander: (TablePlayer, Int, Int, Int) -> Unit,
    onCounter: (TablePlayer, String, Int, Int) -> Unit,
    onLife: (TablePlayer, Int) -> Unit,
    onEditSkills: (TablePlayer, List<com.turnhub.android.domain.InvestigatorSkill>) -> Unit,
    onInvestigatorCard: (TablePlayer, InvestigatorCardSlot, InvestigatorCard?) -> Unit,
    offlineCommander: (Long, Int, Int) -> Int,
    controlsEnabled: Boolean = true,
) {
    val p = palette
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
                .padding(horizontal = 20.dp, vertical = 8.dp)
                .then(if (controlsEnabled) Modifier else Modifier.clearAndSetSemantics {
                    disabled(); contentDescription = "Waiting for the host. Player controls are unavailable."
                }.pointerInput(Unit) {
                    awaitEachGesture {
                        do {
                            val event = awaitPointerEvent(PointerEventPass.Initial)
                            event.changes.forEach { it.consume() }
                        } while (event.changes.any { it.pressed })
                    }
                }),
            verticalArrangement = Arrangement.spacedBy(10.dp),
        ) {
            if (summary.settings.profile.deathMayDie) {
                val member = unit.members.first()
                InvestigatorTracks(unit, summary, member.life ?: 0, { onLife(member, it) },
                    { who, kind, value -> onCounter(who, kind, value, 1) }, Modifier.fillMaxWidth()) { who, action -> onControl(who.seat(), action) }
                InvestigatorAbilities(member, summary,
                    onInsanity = { onCounter(member, StandaloneGame.INSANITY_KIND, it, 1) },
                    onLevel = { slot, level -> onCounter(member, StandaloneGame.SKILL_KIND, level, slot + 1) },
                    onEdit = { onEditSkills(member, it) }, onCard = { slot, card -> onInvestigatorCard(member, slot, card) })
            }
            PlayerMenuItems(unit, summary, partners, onControl, onEliminate, onCommander, onCounter, offlineCommander)
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
    onEliminate: (TablePlayer, Int?) -> Unit,
    onCommander: (TablePlayer, Int, Int, Int) -> Unit,
    onCounter: (TablePlayer, String, Int, Int) -> Unit,
    offlineCommander: (Long, Int, Int) -> Int,
) {
    val p = palette
    val playing = (summary.state == TableState.RUNNING || summary.state == TableState.PAUSED) && summary.settings.profile.magicRules
    // One card for the turn, counters and status: the things a player reaches for most.
    DisclosureCard(if (!playing) "Turn & tools" else if (unit.members.size > 1) "Turn, team counters & status" else "Turn, counters & status",
        initiallyExpanded = true) {
        val actor = unit.members.firstOrNull { it.playerNumber == summary.activePlayerNumber }
        // A cooperative game is won together, from its own card below.
        if (actor != null && summary.state == TableState.RUNNING && !summary.settings.profile.cooperative) {
            ArmedButton("I won the game", "Tap again to confirm your win", Tone.GOOD) { onControl(actor.seat(), "win") }
        }
        val rules = summary.localTimerRules
        if (rules != null && summary.settings.turnTimerEnabled) {
            val holder = actor ?: unit.members.first()
            if (rules.timeout.demerit) Text("Demerits · ${holder.demerits}", color = p.muted)
            if (rules.earnExtensions) {
                Text("Extensions · ${holder.extensionsAvailable} available", color = p.muted)
                val now = rememberNowMs(ticking = summary.state == TableState.RUNNING, intervalMs = 500)
                ToneButton("Extend +${TableClock.format(summary.settings.turnTimerMs / 2)}", {
                    onControl(holder.seat(), "extend-turn")
                }, Modifier.fillMaxWidth(), enabled = actor != null && summary.state == TableState.RUNNING &&
                    holder.extensionsAvailable > 0 && summary.turnExtensionMs == 0L &&
                    (TableClock.turnRemainingMs(summary, now) ?: 0) > 0)
            }
        }
        OpponentPicker(summary, unit.members.first().playerNumber)
        if (playing) {
            val team = unit.members.size > 1
            // A Two-Headed Giant team shares poison and leaves together (CR 810.10, 810.8b): one row, one button.
            if (team) {
                unit.members.firstOrNull { !it.eliminated }?.let { holder ->
                    TeamPoisonRow(holder.counters.poison, true) { onCounter(holder, "poison", it, 1) }
                }
            }
            unit.members.filter { !it.eliminated }.forEach { member ->
                if (team) Text("${member.label} counters and status", color = p.text)
                MagicCountersEditor(member.counters, summary.initiativePlayerNumber == member.playerNumber,
                    false, showPoison = !team, commanderName = member.namedCommander,
                    commanderRarity = rememberCommanderArt(member.namedCommander, member.commanderArt)?.rarity,
                    partnerEnabled = member.playerNumber in partners,
                    onPartnerChange = { on -> if (on) partners.add(member.playerNumber) else partners.remove(member.playerNumber) },
                    phoneCounters = summary.dayNight != null) { kind, value, commander -> onCounter(member, kind, value, commander) }
                ToneButton(
                    if (unit.members.size > 1) "${member.label}: I’m the monarch" else "I’m the monarch",
                    { onControl(member.seat(), "monarch") }, Modifier.fillMaxWidth(), tone = Tone.ACCENT,
                    enabled = member.playerNumber != summary.monarchPlayerNumber,
                )
            }
        }
    }
    if (!summary.settings.profile.countsLife) {
        CardGameMenuItems(unit, summary, { member, kind, value -> onCounter(member, kind, value, 1) },
            { member, action -> onControl(member.seat(), action) })
    }
    if (summary.settings.profile.commanderRules) {
        DisclosureCard("Commander · tax & damage") {
            unit.members.filter { !it.eliminated }.forEach { member ->
                if (unit.members.size > 1) Text(member.label, color = p.text, fontWeight = FontWeight.Bold)
                com.turnhub.android.ui.components.CommanderTaxEditor(
                    member.counters, commanderName = member.namedCommander,
                    commanderRarity = rememberCommanderArt(member.namedCommander, member.commanderArt)?.rarity,
                    partner = member.playerNumber in partners,
                    onPartnerChange = { on -> if (on) partners.add(member.playerNumber) else partners.remove(member.playerNumber) },
                ) { kind, value, commander -> onCounter(member, kind, value, commander) }
                Text("Tax changes cast cost only. Damage below also changes your life.",
                    color = p.muted, style = MaterialTheme.typography.bodySmall)
                Text(
                    if (unit.members.size > 1) "Commander damage to ${member.label}" else "Commander damage received",
                    color = p.muted,
                    style = MaterialTheme.typography.labelLarge,
                )
                val showPartner = member.playerNumber in partners
                // Star Commander allies can't deal each other Commander damage: they aren't listed.
                val allies = summary.starAllies(member.playerNumber)
                if (allies.isNotEmpty()) {
                    Text("Allies can't deal you Commander damage (${allies.joinToString(" and ") { it.label }}).",
                        color = p.faint, style = MaterialTheme.typography.bodySmall)
                }
                summary.players.filter { summary.commanderDamageAllowed(member.playerNumber, it.playerNumber) }.forEach { source ->
                    val damage = member.commanderDamage.firstOrNull { it.sourcePlayer == source.playerNumber }?.damage.orEmpty()
                    fun valueOf(c: Int) = (damage.getOrNull(c - 1) ?: 0) + offlineCommander(member.participantId, source.playerNumber, c)
                    val commanders = if (showPartner || valueOf(2) > 0) listOf(1, 2) else listOf(1)
                    commanders.forEach { c ->
                        val value = valueOf(c)
                        // A named commander says whose damage it is; a partner shows as (2).
                        val label = source.label + when {
                            source.namedCommander != null && c == 1 -> " · ${source.namedCommander}"
                            commanders.size > 1 -> " ($c)"
                            else -> ""
                        }
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
            }
        }
    }
    if (summary.state == TableState.RUNNING || summary.state == TableState.PAUSED) {
        unit.members.firstOrNull { !it.eliminated }?.let { member ->
            DisclosureCard("Leave game") {
                val team = unit.members.size > 1
                ArmedButton(if (team) "Our team concedes" else "Concede", "Tap again to concede", Tone.BAD) {
                    onControl(member.seat(), "concede")
                }
                ToneButton(if (team) "Eliminate this team…" else "Eliminate…", { onEliminate(member, null) }, Modifier.fillMaxWidth())
            }
        }
    }
}

@Composable
internal fun StepButton(glyph: String, label: String, enabled: Boolean, onClick: () -> Unit) {
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
internal fun ArmedButton(label: String, armedLabel: String, tone: Tone, onConfirm: () -> Unit) {
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

/** The game menu's whole-table counters, in the order a player's own counters list them. */
private val EVERYONE_COUNTER_LABELS = listOf("poison" to "☠ Poison", "energy" to "⚡ Energy",
    "experience" to "★ Experience", "treasure" to "◈ Treasure", "rad" to "☢ Rad")
