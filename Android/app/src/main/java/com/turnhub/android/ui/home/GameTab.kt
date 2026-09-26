package com.turnhub.android.ui.home

import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.core.animateIntAsState
import androidx.compose.animation.core.tween
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.ExposedDropdownMenuBox
import androidx.compose.material3.ExposedDropdownMenuDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.MenuAnchorType
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.semantics.LiveRegionMode
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.liveRegion
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.turnhub.android.data.ControlAction
import com.turnhub.android.data.PlayerSessionState
import com.turnhub.android.domain.TableClock
import com.turnhub.android.domain.TableSummary
import com.turnhub.android.domain.TablePlayer
import com.turnhub.android.domain.TurnTimerStatus
import com.turnhub.android.protocol.GameProfile
import com.turnhub.android.protocol.GameSettingsInfo
import com.turnhub.android.protocol.LifeRequestState
import com.turnhub.android.protocol.SessionInfo
import com.turnhub.android.protocol.TableState
import com.turnhub.android.protocol.TurnTimerPhase
import com.turnhub.android.ui.components.AccentButton
import com.turnhub.android.ui.components.BrassCard
import com.turnhub.android.ui.components.CountdownBar
import com.turnhub.android.ui.components.Eyebrow
import com.turnhub.android.ui.components.Gauge
import com.turnhub.android.ui.components.Metric
import com.turnhub.android.ui.components.PlayerAvatar
import com.turnhub.android.ui.components.StatusBadge
import com.turnhub.android.ui.components.StatusRow
import com.turnhub.android.ui.components.Tone
import com.turnhub.android.ui.components.ToneButton
import com.turnhub.android.ui.components.displayName
import com.turnhub.android.ui.theme.palette

/** Everything the Game tab can ask Atlas to do. Each sends at most one request; Atlas decides. */
data class GameActions(
    val onControl: (ControlAction) -> Unit = {},
    val onJoin: () -> Unit = {},
    val onPlayFromPhone: () -> Unit = {},
    val onChangeMyLife: (Int) -> Unit = {},
    val onRequestLife: (target: Int, delta: Int) -> Unit = { _, _ -> },
    val onRespondLife: (requestId: Long, accept: Boolean) -> Unit = { _, _ -> },
    val onCommanderDamage: (source: Int, commander: Int, delta: Int) -> Unit = { _, _, _ -> },
    val onSaveGameSettings: (profile: String?, startingLife: Int?, turnTimerMs: Long?) -> Unit = { _, _, _ -> },
)

/** Atlas's life-request approval window (LIFE_APPROVAL_MS); display only, Atlas enforces it. */
private const val LIFE_APPROVAL_MS = 15_000L

/** The signed-in session, if any. */
internal fun HomeUiState.sessionInfo(): SessionInfo? =
    (player?.session as? PlayerSessionState.SignedIn)?.info

internal fun HomeUiState.me(): TablePlayer? {
    val info = sessionInfo() ?: return null
    if (!info.participating) return null
    return tableSummary?.players?.firstOrNull { it.playerNumber == info.playerNumber }
}

@Composable
fun GameTab(
    uiState: HomeUiState,
    summary: TableSummary,
    nowMs: Long,
    reduceMotion: Boolean,
    actions: GameActions,
    labelFor: (Int) -> String,
) {
    val me = uiState.me()
    Column(verticalArrangement = Arrangement.spacedBy(14.dp)) {
        StageCard(summary, nowMs, labelFor, reduceMotion)
        IncomingLifeRequest(summary, me, nowMs, labelFor, actions)
        SeatCard(uiState, summary, me, actions)
        if (summary.state == TableState.RUNNING || summary.state == TableState.PAUSED ||
            summary.state == TableState.GAME_OVER
        ) {
            LifeCard(summary, me, nowMs, labelFor, actions, busy = uiState.player?.busy == true)
        }
        if (me != null && summary.settings.profile == GameProfile.MTG_COMMANDER &&
            (summary.state == TableState.RUNNING || summary.state == TableState.PAUSED)
        ) {
            CommanderCard(summary, me, labelFor, actions)
        }
        if (summary.state == TableState.LOBBY) {
            SetupCard(summary, uiState.gameSettings, me != null, actions)
        }
        TableCard(summary, labelFor)
    }
}

// --- Stage --------------------------------------------------------------------

@Composable
private fun StageCard(summary: TableSummary, nowMs: Long, labelFor: (Int) -> String, reduceMotion: Boolean) {
    val p = palette
    val timer = TurnTimerStatus.of(summary, nowMs)
    val active = summary.activePlayerNumber?.let(labelFor)
    val (title, subtitle) = when (summary.state) {
        TableState.LOBBY -> "Lobby" to when (summary.players.size) {
            0 -> "Waiting for players to join"
            1 -> "1 player seated; one more to start"
            else -> "${summary.players.size} players seated · ready to start"
        }
        TableState.STARTING -> "Starting" to "The game begins in a moment"
        TableState.RUNNING -> (active?.let { "$it's turn" } ?: "Running") to
            "Turn ${summary.players.firstOrNull { it.playerNumber == summary.activePlayerNumber }?.let { it.turnsCompleted + 1 } ?: "—"}"
        TableState.PAUSED -> "Paused" to when {
            summary.pending.winConfirmationPlayer != null ->
                "${labelFor(summary.pending.winClaimPlayer ?: 0)} claims the win"
            summary.pending.eliminationTargetPlayer != null ->
                "Eliminating ${labelFor(summary.pending.eliminationTargetPlayer!!)}"
            else -> active?.let { "$it was up" } ?: "The table is paused"
        }
        TableState.GAME_OVER -> if (summary.endedInDraw) "Draw" to "The match ended as a draw"
        else "Game over" to "${labelFor(summary.winnerPlayerNumber ?: 0)} wins"
    }
    // The dial: turn countdown when a timer runs, else the turn clock against the 5-minute long-turn mark.
    val (gaugeValue, gaugeCaption, fraction) = when {
        summary.state == TableState.RUNNING || summary.state == TableState.PAUSED -> {
            val remaining = TableClock.turnRemainingMs(summary, nowMs)
            if (summary.settings.turnTimerEnabled && remaining != null) {
                Triple(timer?.value ?: TableClock.format(remaining), timer?.label ?: "Time left",
                    remaining.toFloat() / summary.settings.turnTimerMs)
            } else {
                val elapsed = TableClock.turnElapsedMs(summary, nowMs)
                Triple(timer?.value ?: TableClock.format(elapsed), timer?.label ?: "This turn",
                    (elapsed / 300_000f).coerceAtMost(1f))
            }
        }
        summary.state == TableState.GAME_OVER ->
            Triple(TableClock.format(TableClock.gameElapsedMs(summary, nowMs)), "Game time", 1f)
        else -> Triple("${summary.players.size}", if (summary.players.size == 1) "Player" else "Players",
            summary.players.size / 8f)
    }
    val arcColor = when (timer?.phase) {
        TurnTimerPhase.WARNING -> p.warn
        TurnTimerPhase.EXPIRED -> p.bad
        TurnTimerPhase.LONG_TURN -> p.info
        else -> null
    }
    BrassCard {
        Column(
            Modifier
                .fillMaxWidth()
                .semantics(mergeDescendants = true) { liveRegion = LiveRegionMode.Polite },
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.spacedBy(10.dp),
        ) {
            Gauge(gaugeValue, gaugeCaption, fraction, arcColor = arcColor, reduceMotion = reduceMotion)
            Text(
                summary.settings.profile.displayName().uppercase(),
                color = if (p.dark) p.accentHi else p.accent,
                style = MaterialTheme.typography.labelMedium.copy(fontWeight = FontWeight.Bold, letterSpacing = 2.sp),
            )
            Text(
                title,
                style = MaterialTheme.typography.headlineMedium,
                color = p.text,
                textAlign = TextAlign.Center,
                modifier = Modifier.semantics { heading() },
            )
            Text(subtitle, color = p.muted, style = MaterialTheme.typography.bodyLarge, textAlign = TextAlign.Center)
            StageBadges(summary, nowMs, labelFor, timer)
        }
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun StageBadges(summary: TableSummary, nowMs: Long, labelFor: (Int) -> String, timer: TurnTimerStatus?) {
    FlowRow(horizontalArrangement = Arrangement.spacedBy(6.dp, Alignment.CenterHorizontally), verticalArrangement = Arrangement.spacedBy(6.dp)) {
        StatusBadge("Life ${summary.settings.startingLife}")
        StatusBadge("Timer ${TurnTimerStatus.settingLabel(summary.settings.turnTimerMs).lowercase()}")
        if (summary.state != TableState.LOBBY) {
            StatusBadge("Game ${TableClock.format(TableClock.gameElapsedMs(summary, nowMs))}", Tone.INFO)
        }
        summary.pending.passPlayer?.let {
            val left = TableClock.passGraceRemainingMs(summary, nowMs)
            StatusBadge("${labelFor(it)} passing · ${(left + 999) / 1000}s", Tone.WARN)
        }
        timer?.notice?.let { StatusBadge(timer.label, if (timer.phase == TurnTimerPhase.EXPIRED) Tone.BAD else Tone.WARN) }
        summary.starterPlayerNumber?.takeIf { summary.state == TableState.LOBBY }?.let {
            StatusBadge("${labelFor(it)} starts", Tone.ACTIVE)
        }
    }
    timer?.notice?.let {
        Text(it, color = palette.muted, style = MaterialTheme.typography.bodySmall, textAlign = TextAlign.Center)
    }
}

// --- My seat ------------------------------------------------------------------

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun SeatCard(uiState: HomeUiState, summary: TableSummary, me: TablePlayer?, actions: GameActions) {
    val p = palette
    val panel = uiState.player
    val info = uiState.sessionInfo()
    val signedIn = panel?.signedIn == true
    val name = (panel?.session as? PlayerSessionState.SignedIn)?.name ?: "Not signed in"
    var confirmConcede by remember { mutableStateOf(false) }
    var confirmReset by remember { mutableStateOf(false) }
    if (confirmConcede) {
        ConfirmDialog(
            title = "Concede this game?",
            text = "You leave this game as eliminated. The others play on.",
            confirm = "Concede",
            onConfirm = { confirmConcede = false; actions.onControl(ControlAction.CONCEDE) },
            onDismiss = { confirmConcede = false },
        )
    }
    if (confirmReset) {
        ConfirmDialog(
            title = "Reset the table?",
            text = "Everyone leaves the table and it returns to an empty lobby.",
            confirm = "Reset table",
            onConfirm = { confirmReset = false; actions.onControl(ControlAction.RESET) },
            onDismiss = { confirmReset = false },
        )
    }
    BrassCard(highlight = if (me != null && summary.activePlayerNumber == me.playerNumber &&
        summary.state == TableState.RUNNING) p.active else null) {
        Eyebrow("My seat")
        Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(14.dp)) {
            PlayerAvatar(name, me?.playerNumber ?: 0, me?.avatar, size = 56.dp)
            Column(Modifier.weight(1f)) {
                Text(name, style = MaterialTheme.typography.titleLarge, color = p.text)
                Text(
                    when {
                        !signedIn -> "Sign in to play from this phone. A physical Sigil is optional."
                        me == null -> "Signed in · not at the table"
                        info != null && info.moduleId >= 8 -> "Player ${me.playerNumber} · phone play"
                        else -> "Player ${me.playerNumber} · Sigil ${me.controller.id + 1} + phone"
                    },
                    color = p.muted,
                    style = MaterialTheme.typography.bodyMedium,
                )
            }
        }
        if (me != null) {
            FlowRow(horizontalArrangement = Arrangement.spacedBy(6.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
                if (summary.activePlayerNumber == me.playerNumber && summary.state == TableState.RUNNING) StatusBadge("Your turn", Tone.ACTIVE)
                if (me.eliminated) StatusBadge("Eliminated", Tone.BAD)
                if (summary.winnerPlayerNumber == me.playerNumber) StatusBadge("Winner", Tone.ACCENT)
                me.life?.let { StatusBadge("Life $it", Tone.GOOD) }
            }
        }
        panel?.feedback?.let { feedback ->
            Text(
                feedback.message,
                color = if (feedback.isError) p.bad else p.good,
                style = MaterialTheme.typography.bodyMedium,
                modifier = Modifier.semantics { liveRegion = LiveRegionMode.Polite },
            )
        }
        val busy = panel?.busy == true
        when {
            !signedIn -> AccentButton("Sign in or pick a profile", actions.onPlayFromPhone, Modifier.fillMaxWidth(), enabled = panel != null)
            me == null -> AccentButton(
                if (summary.state == TableState.LOBBY) "Join table" else "Join in the lobby",
                actions.onJoin,
                Modifier.fillMaxWidth(),
                enabled = !busy && summary.state == TableState.LOBBY,
            )
            summary.state == TableState.LOBBY -> {
                ControlGrid {
                    ToneButton("I go first", { actions.onControl(ControlAction.SELECT_STARTER) }, Modifier.weight(1f), enabled = !busy)
                    AccentButton("Start game", { actions.onControl(ControlAction.START) }, Modifier.weight(1f),
                        enabled = !busy && summary.players.size >= 2)
                }
                ControlGrid {
                    ToneButton("Leave table", { actions.onControl(ControlAction.LEAVE) }, Modifier.weight(1f), enabled = !busy)
                    ToneButton("Reset table", { confirmReset = true }, Modifier.weight(1f), tone = Tone.BAD, enabled = !busy)
                }
            }
            summary.state == TableState.STARTING ->
                ToneButton("Cancel countdown", { actions.onControl(ControlAction.CANCEL_START) }, Modifier.fillMaxWidth(), tone = Tone.WARN, enabled = !busy)
            summary.state == TableState.RUNNING && !me.eliminated -> {
                val myTurn = summary.activePlayerNumber == me.playerNumber
                val passing = summary.pending.passPlayer == me.playerNumber
                AccentButton(
                    if (passing) "Cancel pending pass" else "Pass turn",
                    { actions.onControl(ControlAction.PASS) },
                    Modifier.fillMaxWidth().height(64.dp),
                    enabled = !busy && myTurn,
                )
                ControlGrid {
                    ToneButton("Pause", { actions.onControl(ControlAction.PAUSE_RESUME) }, Modifier.weight(1f), enabled = !busy)
                    ToneButton("Claim win", { actions.onControl(ControlAction.CLAIM_WIN) }, Modifier.weight(1f), tone = Tone.GOOD, enabled = !busy && myTurn)
                }
                ToneButton("Concede", { confirmConcede = true }, Modifier.fillMaxWidth(), tone = Tone.BAD, enabled = !busy)
            }
            summary.state == TableState.PAUSED && !me.eliminated -> {
                val pending = summary.pending
                when {
                    pending.winConfirmationPlayer == me.playerNumber -> {
                        Text(
                            "${pending.winClaimPlayer?.let { "Player $it" } ?: "A player"} claims the win. Do you agree?",
                            color = p.text,
                        )
                        ControlGrid {
                            AccentButton("Confirm win", { actions.onControl(ControlAction.CONFIRM_WIN) }, Modifier.weight(1f), enabled = !busy)
                            ToneButton("Deny claim", { actions.onControl(ControlAction.DENY_WIN) }, Modifier.weight(1f), tone = Tone.BAD, enabled = !busy)
                        }
                    }
                    pending.winConfirmationPlayer == null && pending.eliminationTargetPlayer == null -> {
                        AccentButton("Resume game", { actions.onControl(ControlAction.PAUSE_RESUME) }, Modifier.fillMaxWidth().height(64.dp), enabled = !busy)
                        ControlGrid {
                            if (summary.activePlayerNumber == me.playerNumber) {
                                ToneButton("Claim win", { actions.onControl(ControlAction.CLAIM_WIN) }, Modifier.weight(1f), tone = Tone.GOOD, enabled = !busy)
                            }
                            ToneButton("Concede", { confirmConcede = true }, Modifier.weight(1f), tone = Tone.BAD, enabled = !busy)
                        }
                    }
                    else -> Text("Waiting for the table to decide.", color = p.muted)
                }
            }
            summary.state == TableState.GAME_OVER -> ControlGrid {
                AccentButton("Rematch", { actions.onControl(ControlAction.REMATCH) }, Modifier.weight(1f), enabled = !busy)
                ToneButton("Reset table", { confirmReset = true }, Modifier.weight(1f), tone = Tone.BAD, enabled = !busy)
            }
            me.eliminated -> Text("You are out of this game. You can still follow the table here.", color = p.muted)
        }
    }
}

@Composable
private fun ControlGrid(content: @Composable androidx.compose.foundation.layout.RowScope.() -> Unit) {
    Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp), content = content)
}

@Composable
internal fun ConfirmDialog(
    title: String,
    text: String,
    confirm: String,
    onConfirm: () -> Unit,
    onDismiss: () -> Unit,
) {
    val p = palette
    AlertDialog(
        onDismissRequest = onDismiss,
        containerColor = p.surface,
        title = { Text(title) },
        text = { Text(text, color = p.muted) },
        confirmButton = { TextButton(onClick = onConfirm) { Text(confirm, color = p.bad, fontWeight = FontWeight.Bold) } },
        dismissButton = { TextButton(onClick = onDismiss) { Text("Cancel", color = p.text) } },
    )
}

// --- Life ---------------------------------------------------------------------

/** A request aimed at me: the banner the portal pins to the top of the screen. */
@Composable
private fun IncomingLifeRequest(
    summary: TableSummary,
    me: TablePlayer?,
    nowMs: Long,
    labelFor: (Int) -> String,
    actions: GameActions,
) {
    val p = palette
    if (me == null) return
    val request = me.lifeRequest?.takeIf { it.state == LifeRequestState.PENDING && it.target == me.playerNumber } ?: return
    val age = ageOf(summary, request.requestedAtMs, nowMs)
    val left = (LIFE_APPROVAL_MS - age).coerceAtLeast(0)
    BrassCard(highlight = p.warn) {
        Eyebrow("Life change request")
        Text(
            "${labelFor(request.actor)} wants to ${if (request.delta < 0) "take ${-request.delta} from" else "add ${request.delta} to"} your life" +
                (me.life?.let { " (${it} → ${it + request.delta})" } ?: "") + ".",
            color = p.text,
            style = MaterialTheme.typography.bodyLarge,
            modifier = Modifier.semantics { liveRegion = LiveRegionMode.Assertive },
        )
        CountdownBar(left / LIFE_APPROVAL_MS.toFloat(), p.warn)
        Text("Atlas accepts it in ${(left + 999) / 1000} s unless you answer.", color = p.muted, style = MaterialTheme.typography.bodySmall)
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            ToneButton("Accept", { actions.onRespondLife(request.id, true) }, Modifier.weight(1f), tone = Tone.GOOD)
            ToneButton("Reject", { actions.onRespondLife(request.id, false) }, Modifier.weight(1f), tone = Tone.BAD)
        }
    }
}

/** How long ago Atlas stamped [atMs], on Atlas's clock, rendered forward locally. */
private fun ageOf(summary: TableSummary, atMs: Long, nowMs: Long): Long {
    val sinceReceipt = (nowMs - summary.receivedAtMs).coerceAtLeast(0)
    val atlasNow = summary.sampledAtMs + sinceReceipt
    return ((atlasNow - atMs) and 0xFFFF_FFFFL).coerceAtMost(LIFE_APPROVAL_MS)
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun LifeCard(
    summary: TableSummary,
    me: TablePlayer?,
    nowMs: Long,
    labelFor: (Int) -> String,
    actions: GameActions,
    busy: Boolean,
) {
    val p = palette
    var requestTarget by remember { mutableStateOf<TablePlayer?>(null) }
    requestTarget?.let { target ->
        LifeRequestDialog(
            target = target,
            onSend = { delta -> requestTarget = null; actions.onRequestLife(target.playerNumber, delta) },
            onDismiss = { requestTarget = null },
        )
    }
    val canAct = me != null && !me.eliminated &&
        (summary.state == TableState.RUNNING || summary.state == TableState.PAUSED)
    BrassCard {
        Eyebrow("Life")
        FlowRow(
            horizontalArrangement = Arrangement.spacedBy(10.dp),
            verticalArrangement = Arrangement.spacedBy(10.dp),
            maxItemsInEachRow = 2,
        ) {
            summary.players.forEach { player ->
                LifeTile(
                    player = player,
                    active = player.playerNumber == summary.activePlayerNumber,
                    mine = player.playerNumber == me?.playerNumber,
                    winner = player.playerNumber == summary.winnerPlayerNumber,
                    onRequest = if (canAct && player.playerNumber != me?.playerNumber && !player.eliminated) {
                        { requestTarget = player }
                    } else null,
                    modifier = Modifier.weight(1f),
                )
            }
        }
        if (me != null && canAct) {
            MyLifePad(me.life, actions.onChangeMyLife, busy)
            Text(
                "Your own changes apply immediately. Tap another player to ask for a change; " +
                    "they have 15 seconds to respond before Atlas accepts it.",
                color = p.muted,
                style = MaterialTheme.typography.bodySmall,
            )
        }
        // My outgoing requests.
        summary.players.mapNotNull { it.lifeRequest }
            .filter { it.actor == me?.playerNumber && it.state == LifeRequestState.PENDING }
            .forEach {
                Text(
                    "Waiting for ${labelFor(it.target)} to answer your ${if (it.delta > 0) "+" else ""}${it.delta} request.",
                    color = p.info,
                    style = MaterialTheme.typography.bodySmall,
                )
            }
    }
}

@Composable
private fun LifeTile(
    player: TablePlayer,
    active: Boolean,
    mine: Boolean,
    winner: Boolean,
    onRequest: (() -> Unit)?,
    modifier: Modifier = Modifier,
) {
    val p = palette
    val life = player.life
    val shown by animateIntAsState(life ?: 0, tween(400), label = "life")
    var last by remember { mutableIntStateOf(life ?: 0) }
    var delta by remember { mutableIntStateOf(0) }
    if (life != null && life != last) {
        delta = life - last
        last = life
    }
    val border = when {
        winner -> p.accent
        active -> p.active
        mine -> p.lineStrong
        else -> p.line
    }
    Column(
        modifier
            .clip(RoundedCornerShape(14.dp))
            .background(if (mine) p.surface3 else p.surface2)
            .border(if (active || winner) 2.dp else 1.dp, border, RoundedCornerShape(14.dp))
            .then(if (onRequest != null) Modifier.clickable(onClickLabel = "Request a life change", onClick = onRequest) else Modifier)
            .padding(12.dp)
            .semantics(mergeDescendants = true) {
                contentDescription = "${player.label}, life ${life ?: "not set"}" +
                    (if (active) ", active" else "") + (if (player.eliminated) ", eliminated" else "")
            },
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            PlayerAvatar(player.label, player.playerNumber, player.avatar, size = 28.dp)
            Text(
                player.label + if (mine) " (you)" else "",
                color = p.text,
                style = MaterialTheme.typography.labelLarge,
                maxLines = 1,
            )
        }
        Box(contentAlignment = Alignment.TopEnd) {
            Text(
                if (life == null) "—" else shown.toString(),
                color = if (player.eliminated) p.faint else p.text,
                style = MaterialTheme.typography.displaySmall,
                modifier = Modifier.padding(horizontal = 18.dp),
            )
            if (delta != 0) {
                Text(
                    (if (delta > 0) "+" else "") + delta,
                    color = if (delta > 0) p.good else p.bad,
                    style = MaterialTheme.typography.labelLarge.copy(fontWeight = FontWeight.ExtraBold),
                )
            }
        }
        Text(
            when {
                winner -> "Winner"
                player.eliminated -> "Eliminated"
                active -> "Active"
                onRequest != null -> "Tap to request"
                else -> "Player ${player.playerNumber}"
            },
            color = when {
                winner -> p.accent
                player.eliminated -> p.bad
                active -> p.active
                else -> p.faint
            },
            style = MaterialTheme.typography.labelSmall,
        )
    }
}

@Composable
private fun MyLifePad(life: Int?, onChange: (Int) -> Unit, busy: Boolean) {
    val p = palette
    var custom by rememberSaveable { mutableStateOf("") }
    Column(
        Modifier
            .fillMaxWidth()
            .clip(RoundedCornerShape(12.dp))
            .background(p.inset)
            .border(1.dp, p.line, RoundedCornerShape(12.dp))
            .padding(12.dp),
        verticalArrangement = Arrangement.spacedBy(8.dp),
    ) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text("My life", color = p.muted, style = MaterialTheme.typography.titleSmall, modifier = Modifier.weight(1f))
            Text(life?.toString() ?: "—", color = p.text, style = MaterialTheme.typography.headlineLarge)
        }
        Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            listOf(-5, -1, 1, 5).forEach { step ->
                ToneButton(
                    (if (step > 0) "+" else "−") + kotlin.math.abs(step),
                    { onChange(step) },
                    Modifier.weight(1f),
                    tone = if (step > 0) Tone.GOOD else Tone.BAD,
                    enabled = !busy,
                )
            }
        }
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalAlignment = Alignment.CenterVertically) {
            OutlinedTextField(
                value = custom,
                onValueChange = { custom = it.filter { c -> c.isDigit() || c == '-' }.take(8) },
                label = { Text("Custom change") },
                singleLine = true,
                keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number),
                modifier = Modifier.weight(1f),
            )
            AccentButton("Apply", {
                custom.toIntOrNull()?.takeIf { it != 0 }?.let(onChange)
                custom = ""
            }, enabled = !busy && (custom.toIntOrNull() ?: 0) != 0)
        }
    }
}

@Composable
private fun LifeRequestDialog(target: TablePlayer, onSend: (Int) -> Unit, onDismiss: () -> Unit) {
    val p = palette
    var amount by remember { mutableIntStateOf(-1) }
    AlertDialog(
        onDismissRequest = onDismiss,
        containerColor = p.surface,
        title = { Text("Change ${target.label}'s life") },
        text = {
            Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
                Text(
                    "${target.label} has 15 seconds to accept or reject. Atlas accepts it if they don't answer.",
                    color = p.muted,
                )
                Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    ToneButton("−5", { amount -= 5 }, Modifier.weight(1f), tone = Tone.BAD)
                    ToneButton("−1", { amount -= 1 }, Modifier.weight(1f), tone = Tone.BAD)
                    Text(
                        (if (amount > 0) "+" else "") + amount,
                        style = MaterialTheme.typography.headlineMedium,
                        color = if (amount < 0) p.bad else if (amount > 0) p.good else p.text,
                        modifier = Modifier.widthIn(min = 56.dp),
                        textAlign = TextAlign.Center,
                    )
                    ToneButton("+1", { amount += 1 }, Modifier.weight(1f), tone = Tone.GOOD)
                    ToneButton("+5", { amount += 5 }, Modifier.weight(1f), tone = Tone.GOOD)
                }
                target.life?.let { Text("$it → ${it + amount}", color = p.text, modifier = Modifier.fillMaxWidth(), textAlign = TextAlign.Center) }
            }
        },
        confirmButton = {
            TextButton(onClick = { onSend(amount) }, enabled = amount != 0) { Text("Send request", fontWeight = FontWeight.Bold) }
        },
        dismissButton = { TextButton(onClick = onDismiss) { Text("Cancel", color = p.text) } },
    )
}

// --- Commander ----------------------------------------------------------------

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun CommanderCard(summary: TableSummary, me: TablePlayer, labelFor: (Int) -> String, actions: GameActions) {
    val p = palette
    val sources = summary.players.filter { it.playerNumber != me.playerNumber }
    var source by remember(sources) { mutableIntStateOf(sources.firstOrNull()?.playerNumber ?: 0) }
    var commander by remember { mutableIntStateOf(1) }
    BrassCard {
        Eyebrow("My received Commander damage")
        Text(
            "Record damage from each commander separately. Adding damage also subtracts life; a negative correction restores it.",
            color = p.muted,
            style = MaterialTheme.typography.bodySmall,
        )
        sources.forEach { from ->
            val damage = me.commanderDamage.firstOrNull { it.sourcePlayer == from.playerNumber }?.damage ?: listOf(0, 0)
            Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                PlayerAvatar(from.label, from.playerNumber, from.avatar, size = 30.dp)
                Text(from.label, color = p.text, modifier = Modifier.weight(1f))
                Text("C1 ${damage.getOrElse(0) { 0 }}", color = if ((damage.getOrElse(0) { 0 }) >= 21) p.bad else p.text)
                Text("C2 ${damage.getOrElse(1) { 0 }}", color = if ((damage.getOrElse(1) { 0 }) >= 21) p.bad else p.text)
            }
        }
        if (sources.isNotEmpty()) {
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                ChoiceDropdown(
                    label = "Commander owner",
                    options = sources.map { it.playerNumber to it.label },
                    selected = source,
                    onSelect = { source = it },
                    modifier = Modifier.weight(1f),
                )
                ChoiceDropdown(
                    label = "Commander",
                    options = listOf(1 to "Commander 1", 2 to "Commander 2"),
                    selected = commander,
                    onSelect = { commander = it },
                    modifier = Modifier.weight(1f),
                )
            }
            Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                listOf(-1, 1, 3, 5).forEach { d ->
                    ToneButton(
                        (if (d > 0) "+" else "−") + kotlin.math.abs(d),
                        { actions.onCommanderDamage(source, commander, d) },
                        Modifier.weight(1f),
                        tone = if (d > 0) Tone.BAD else Tone.GOOD,
                    )
                }
            }
            Text("Players decide when to concede; damage never eliminates anyone automatically.", color = p.faint, style = MaterialTheme.typography.bodySmall)
        }
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
internal fun <T> ChoiceDropdown(
    label: String,
    options: List<Pair<T, String>>,
    selected: T,
    onSelect: (T) -> Unit,
    modifier: Modifier = Modifier,
    enabled: Boolean = true,
) {
    var open by remember { mutableStateOf(false) }
    ExposedDropdownMenuBox(expanded = open, onExpandedChange = { if (enabled) open = it }, modifier = modifier) {
        OutlinedTextField(
            value = options.firstOrNull { it.first == selected }?.second ?: "",
            onValueChange = {},
            readOnly = true,
            enabled = enabled,
            label = { Text(label) },
            trailingIcon = { ExposedDropdownMenuDefaults.TrailingIcon(expanded = open) },
            modifier = Modifier.menuAnchor(MenuAnchorType.PrimaryNotEditable).fillMaxWidth(),
        )
        ExposedDropdownMenu(expanded = open, onDismissRequest = { open = false }) {
            options.forEach { (value, text) ->
                DropdownMenuItem(text = { Text(text) }, onClick = { onSelect(value); open = false })
            }
        }
    }
}

// --- Setup --------------------------------------------------------------------

@Composable
private fun SetupCard(summary: TableSummary, info: GameSettingsInfo?, seated: Boolean, actions: GameActions) {
    val p = palette
    val current = info?.settings ?: summary.settings
    val editable = seated && info?.canEdit == true
    var profile by remember(current.profile) { mutableStateOf(current.profile) }
    var life by remember(current.startingLife) { mutableStateOf(current.startingLife.toString()) }
    val presets = info?.turnTimerPresetsMs?.takeIf { it.isNotEmpty() } ?: listOf(0L, 60_000L, 90_000L, 120_000L, 180_000L)
    val timerOptions = (listOf(0L) + presets).distinct().let { if (current.turnTimerMs in it) it else it + current.turnTimerMs }
    var timer by remember(current.turnTimerMs) { mutableStateOf(current.turnTimerMs) }
    BrassCard {
        Eyebrow("Game setup")
        ChoiceDropdown(
            label = "Game profile",
            options = GameProfile.entries.map { it to it.displayName() },
            selected = profile,
            onSelect = {
                profile = it
                life = when (it) {
                    GameProfile.MTG -> "20"
                    GameProfile.MTG_COMMANDER -> "40"
                    GameProfile.YUGIOH -> "8000"
                    GameProfile.GENERIC -> life
                }
            },
            enabled = editable,
        )
        OutlinedTextField(
            value = life,
            onValueChange = { life = it.filter(Char::isDigit).take(7) },
            label = { Text("Starting life") },
            singleLine = true,
            enabled = editable,
            keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number),
            modifier = Modifier.fillMaxWidth(),
        )
        ChoiceDropdown(
            label = "Turn timer",
            options = timerOptions.map { it to TurnTimerStatus.settingLabel(it) },
            selected = timer,
            onSelect = { timer = it },
            enabled = editable,
        )
        Text(
            "Off: no countdown; a gentle cue appears after 5 minutes. With a timer, Atlas warns 10 seconds " +
                "before time runs out. Running out of time never passes the turn.",
            color = p.muted,
            style = MaterialTheme.typography.bodySmall,
        )
        AccentButton(
            "Save game settings",
            { actions.onSaveGameSettings(profile.wireValue, life.toIntOrNull(), timer) },
            Modifier.fillMaxWidth(),
            enabled = editable && life.toIntOrNull() != null,
        )
        if (!editable) {
            Text(
                if (seated) "Settings can change only in the lobby." else "Join the table to change the setup.",
                color = p.faint,
                style = MaterialTheme.typography.bodySmall,
            )
        }
    }
}

// --- Table --------------------------------------------------------------------

@Composable
private fun TableCard(summary: TableSummary, labelFor: (Int) -> String) {
    val sigils = summary.players.map { it.controller }.filter { it.kind == com.turnhub.android.domain.ControllerHandle.Kind.PHYSICAL }.distinct().size
    BrassCard {
        Eyebrow("Table")
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            Metric("Players", summary.players.size.toString(), Modifier.weight(1f))
            Metric("Sigils", sigils.toString(), Modifier.weight(1f))
            Metric("Starter", summary.starterPlayerNumber?.let { "P$it" } ?: "—", Modifier.weight(1f))
        }
        Column {
            StatusRow("State", summary.state.name.lowercase().replace('_', ' ').replaceFirstChar { it.uppercase() })
            StatusRow("Active", summary.activePlayerNumber?.let(labelFor) ?: "None")
            StatusRow("Starting player", summary.starterPlayerNumber?.let(labelFor) ?: "None")
            StatusRow("Win response", summary.pending.winConfirmationPlayer?.let(labelFor) ?: "None")
            StatusRow("Elimination target", summary.pending.eliminationTargetPlayer?.let(labelFor) ?: "None")
            StatusRow("Winner", summary.winnerPlayerNumber?.let(labelFor) ?: if (summary.endedInDraw) "Draw" else "None")
        }
    }
}
