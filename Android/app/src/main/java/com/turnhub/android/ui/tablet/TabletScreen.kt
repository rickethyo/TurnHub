package com.turnhub.android.ui.tablet

import android.app.Activity
import android.content.Context
import android.content.ContextWrapper
import androidx.activity.compose.BackHandler
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawingPadding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.setValue
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalView
import androidx.compose.ui.semantics.LiveRegionMode
import androidx.compose.ui.semantics.liveRegion
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.unit.dp
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsControllerCompat
import com.turnhub.android.data.AtlasTablet
import com.turnhub.android.data.ProfileSecret
import com.turnhub.android.data.TabletSeat
import com.turnhub.android.data.TabletState
import com.turnhub.android.domain.TablePlayer
import com.turnhub.android.domain.TableSummary
import com.turnhub.android.protocol.AccountPermission
import com.turnhub.android.protocol.SessionInfo
import com.turnhub.android.protocol.TableState
import com.turnhub.android.ui.components.AccentButton
import com.turnhub.android.ui.components.BrassCard
import com.turnhub.android.ui.components.OfflineBanner
import com.turnhub.android.ui.components.Eyebrow
import com.turnhub.android.ui.components.ToneButton
import com.turnhub.android.ui.components.AmbientBackground
import com.turnhub.android.ui.theme.palette

/** What the tablet screen can ask of the app; [run] sends one tablet request. */
data class TabletActions(
    val run: (suspend AtlasTablet.() -> Unit) -> Unit = {},
    val onDismissMessage: () -> Unit = {},
    val onDismissPin: () -> Unit = {},
    val onDismissCode: () -> Unit = {},
    /** Opens the app's sign-in picker (tablet mode needs a signed-in account). */
    val onSignIn: () -> Unit = {},
)

/**
 * Native shared tablet mode: this device lies in the middle of
 * the table and every player has a panel facing their seat. Atlas grants it
 * with a presence code and decides every outcome; the screen draws the state
 * snapshot and sends the `/api/tablet/` routes.
 */
@Composable
fun TabletScreen(
    summary: TableSummary?,
    session: SessionInfo?,
    signedIn: Boolean,
    tablet: TabletState,
    actions: TabletActions,
    reduceMotion: Boolean,
    onClose: () -> Unit,
    modifier: Modifier = Modifier,
    /** Atlas isn't answering: [summary] is its last known state, shown read-only. */
    offline: Boolean = false,
    onFollowGame: (Int) -> Unit = {},
    switching: Boolean = false,
    onEditBackground: ((TablePlayer) -> Unit)? = null,
    canEditBackground: (TablePlayer) -> Boolean = { false },
    onBackToSetup: () -> Unit = onClose,
) {
    var previousState by remember { mutableStateOf(summary?.state) }
    androidx.compose.runtime.LaunchedEffect(summary?.state) {
        if (previousState in listOf(TableState.RUNNING, TableState.PAUSED) && summary?.state == TableState.LOBBY) onBackToSetup()
        previousState = summary?.state
    }
    val scopedActions = actions.copy(run = { block -> actions.run { inGame(summary?.game ?: 1, block) } })
    val p = palette
    KeepScreenOn()
    BackHandler(onBack = onClose)
    tablet.pinPrompt?.let { prompt -> PinDialog(prompt.name, onSubmit = { pin -> scopedActions.run { seatSaved(prompt.profileId, prompt.name, pin) } }, onDismiss = scopedActions.onDismissPin) }
    val granted = signedIn && session?.tablet == true
    AmbientBackground(reduceMotion, modifier.fillMaxSize(), quiet = true) {
    Column(Modifier.fillMaxSize()) {
        if (summary != null) com.turnhub.android.ui.home.GameSelector(summary, onFollowGame, enabled = !offline && !switching && !tablet.busy)
        androidx.compose.runtime.key(summary?.atlasId, summary?.bootId, summary?.game) {
            Box(Modifier.weight(1f).fillMaxWidth()) {
                when {
                    summary == null -> Gate(tablet, scopedActions, onClose) {
                        Text("Connect to Atlas first, then open tablet mode again.", color = p.muted)
                    }
                    !granted -> Gate(tablet, scopedActions, onClose) {
                        GateSteps(signedIn, session?.has(AccountPermission.TABLET_ACCESS) == true, tablet, scopedActions)
                    }
                    session.game != summary.game -> Gate(tablet, scopedActions, onClose) {
                        Text("Refreshing the selected game…", color = p.muted)
                    }
                    summary.state == TableState.LOBBY -> Lobby(summary, tablet, scopedActions, onClose, onEditBackground, canEditBackground)
                    else -> {
                        Immersive()
                        TabletTable(summary, tablet, { block -> scopedActions.run { this.block() } }, scopedActions.onDismissMessage, reduceMotion, onClose, offline)
                    }
                }
                if (offline && summary != null) {
                    val playing = granted && summary.state != TableState.LOBBY
                    if (playing) OfflineStrip(tablet.waiting, Modifier.align(Alignment.TopCenter))
                    else OfflineCover(summary, onClose)
                }
            }
        }
    }
    }
}

/**
 * During a game while Atlas isn't answering: the table stays usable for life
 * and Commander damage, which wait for Atlas; this says so without covering it.
 */
@Composable
private fun OfflineStrip(waiting: Int, modifier: Modifier) {
    val p = palette
    val saved = when (waiting) {
        0 -> "Life and Commander damage are saved for Atlas"
        1 -> "1 change saved for Atlas"
        else -> "$waiting changes saved for Atlas"
    }
    Text(
        "Atlas is offline, reconnecting. $saved.",
        color = p.text,
        style = MaterialTheme.typography.labelLarge,
        modifier = modifier
            .safeDrawingPadding()
            .padding(top = 6.dp)
            .background(p.surface, RoundedCornerShape(99.dp))
            .border(1.dp, p.warn, RoundedCornerShape(99.dp))
            .padding(horizontal = 14.dp, vertical = 6.dp)
            .semantics { liveRegion = LiveRegionMode.Polite },
    )
}

/**
 * Outside a game (lobby, or before the grant), keeps the last known screen
 * while Atlas isn't answering, dimmed and untouchable, with what happened and
 * a way out. Lifts by itself on reconnect.
 */
@Composable
private fun OfflineCover(summary: TableSummary, onClose: () -> Unit) {
    val p = palette
    Box(
        Modifier
            .fillMaxSize()
            .background(p.bg.copy(alpha = 0.45f))
            .pointerInput(Unit) {
                awaitPointerEventScope {
                    while (true) awaitPointerEvent().changes.forEach { it.consume() }
                }
            },
    ) {
        Column(
            Modifier.align(Alignment.TopCenter).safeDrawingPadding().padding(12.dp).widthIn(max = 520.dp).fillMaxWidth(),
            verticalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            OfflineBanner(summary.receivedAtMs, Modifier.fillMaxWidth())
            ToneButton("Close tablet mode", onClose, Modifier.fillMaxWidth())
        }
    }
}

/** The table screen stays on while tablet mode is open. */
@Composable
internal fun KeepScreenOn() {
    val view = LocalView.current
    DisposableEffect(view) {
        view.keepScreenOn = true
        onDispose { view.keepScreenOn = false }
    }
}

/** During play the system bars hide; a swipe from the edge shows them briefly. */
@Composable
internal fun Immersive() {
    val view = LocalView.current
    DisposableEffect(view) {
        val window = view.context.findActivity()?.window
        val controller = window?.let { WindowCompat.getInsetsController(it, view) }
        controller?.systemBarsBehavior = WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
        controller?.hide(WindowInsetsCompat.Type.systemBars())
        onDispose { controller?.show(WindowInsetsCompat.Type.systemBars()) }
    }
}

private tailrec fun Context.findActivity(): Activity? = when (this) {
    is Activity -> this
    is ContextWrapper -> baseContext.findActivity()
    else -> null
}

// --- the grant ----------------------------------------------------------------------

@Composable
private fun Gate(tablet: TabletState, actions: TabletActions, onClose: () -> Unit, steps: @Composable () -> Unit) {
    val p = palette
    Column(
        Modifier.fillMaxSize().safeDrawingPadding().verticalScroll(rememberScrollState()).padding(16.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.Center,
    ) {
        BrassCard(Modifier.widthIn(max = 520.dp).fillMaxWidth()) {
            Eyebrow("Tablet mode")
            Text(
                "One shared screen in the middle of the table. Every player gets a panel facing their seat " +
                    "for life, Commander damage and passing the turn.",
                color = p.text,
            )
            steps()
            tablet.message?.let { Feedback(it.message, it.isError) }
            ToneButton("Back", onClose, Modifier.fillMaxWidth())
        }
    }
}

@Composable
private fun GateSteps(signedIn: Boolean, tabletAccess: Boolean, tablet: TabletState, actions: TabletActions) {
    val p = palette
    if (!signedIn) {
        Text("Sign in first. Any account can turn on tablet mode.", color = p.muted)
        AccentButton("Sign in", actions.onSignIn, Modifier.fillMaxWidth())
        return
    }
    if (tabletAccess) {
        Text("This account has tablet access, so no code is needed.", color = p.muted)
        AccentButton("Use this device as the tablet", { actions.run { enable() } }, Modifier.fillMaxWidth(), enabled = !tablet.busy)
        return
    }
    if (!tablet.codePrompt) {
        Text("If an Admin turned the table code on, Atlas shows a code on its screen to prove this device is at the table.", color = p.muted)
        AccentButton("Use this device as the tablet", { actions.run { requestCode() } }, Modifier.fillMaxWidth(), enabled = !tablet.busy)
        return
    }
    var code by rememberSaveable { mutableStateOf("") }
    Text("Enter the six digits the Atlas screen shows now.", color = p.muted)
    OutlinedTextField(
        value = code,
        onValueChange = { code = it.filter(Char::isDigit).take(6) },
        label = { Text("Code from Atlas") },
        singleLine = true,
        keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.NumberPassword, imeAction = ImeAction.Done),
        keyboardActions = KeyboardActions(onDone = { if (code.length == 6) actions.run { confirmCode(code) } }),
        modifier = Modifier.fillMaxWidth(),
    )
    AccentButton("Use this device as the tablet", { actions.run { confirmCode(code) } }, Modifier.fillMaxWidth(),
        enabled = code.length == 6 && !tablet.busy)
    TextButton(onClick = actions.onDismissCode) { Text("Cancel", color = p.muted) }
}

@Composable
internal fun Feedback(message: String, isError: Boolean) {
    val p = palette
    Text(message, color = if (isError) p.bad else p.good, style = MaterialTheme.typography.bodyMedium)
}

// --- the lobby ----------------------------------------------------------------------

/** Any seated player carries lobby settings and Start; Atlas treats them alike. */
internal fun TableSummary.anySeat(): TabletSeat? = players.firstOrNull()?.seat()

internal fun TablePlayer.seat(): TabletSeat = TabletSeat(controller.id, slot)

@Composable
private fun Lobby(summary: TableSummary, tablet: TabletState, actions: TabletActions, onClose: () -> Unit,
    onEditBackground: ((TablePlayer) -> Unit)?, canEditBackground: (TablePlayer) -> Boolean) {
    val p = palette
    LaunchedEffect(summary.players.size) { actions.run { loadSavedProfiles() } }
    var confirmLeave by remember { mutableStateOf(false) }
    if (confirmLeave) {
        AlertDialog(
            onDismissRequest = { confirmLeave = false },
            containerColor = p.surface,
            title = { Text("Leave tablet mode?") },
            text = { Text("This device stops acting for the table. Seated players stay seated.", color = p.muted) },
            confirmButton = {
                TextButton(onClick = { confirmLeave = false; actions.run { disable() }; onClose() }) {
                    Text("Leave", color = p.bad, fontWeight = FontWeight.Bold)
                }
            },
            dismissButton = { TextButton(onClick = { confirmLeave = false }) { Text("Cancel", color = p.text) } },
        )
    }
    val players = summary.players.sortedBy { it.playerNumber }
    val seat = summary.anySeat()
    val seated = players.mapNotNull { it.profileId }.toSet()
    TableLobby(
        summary = summary,
        actions = TableLobbyActions(
            add = { name -> actions.run { seatNew(name) } },
            select = { player -> actions.run { seatSaved(player.id, player.name) } },
            remove = { player -> player.profileId?.let { id -> actions.run { unseat(id) } } },
            canRemove = { it.profileId != null },
            clear = { actions.run { clearSeats(players.mapNotNull { it.profileId }) } },
            swap = { a, b ->
                if (!tablet.busy && a in players.indices && b in players.indices && a != b) {
                    val lo = minOf(a, b)
                    val hi = maxOf(a, b)
                    val low = players[lo].seat()
                    val high = players[hi].seat()
                    actions.run {
                        var ok = true
                        repeat(hi - lo) { if (ok) ok = control(low, "move-later") }
                        repeat(hi - lo - 1) { if (ok) ok = control(high, "move-earlier") }
                    }
                }
            },
            settings = { profile, life, teams -> seat?.let { actions.run { saveSettings(it, profile?.wireValue, life, teams) } } },
            timer = { ms -> seat?.let { actions.run { saveSettings(it, null, null, null, turnTimerMs = ms) } } },
            zeroLife = { on -> seat?.let { actions.run { setAutoEliminateAtZeroLife(it, on) } } },
            start = { seat?.let { actions.run { control(it, "start") } } },
            chooseStarter = { player -> actions.run { control(player.seat(), "starter") } },
            startWith = { player ->
                seat?.let { starter ->
                    val current = summary.starterPlayerNumber
                    // Atlas clears a choice made again: that is how Random is set before starting.
                    val toggle = players.firstOrNull { it.playerNumber == (player?.playerNumber ?: current) }
                    actions.run {
                        val ok = if (toggle != null && current != player?.playerNumber) control(toggle.seat(), "starter") else true
                        if (ok) control(starter, "start")
                    }
                }
            },
            houseRules = { on -> seat?.let { actions.run { saveSettings(it, null, null, null, houseRules = on) } } },
            background = onEditBackground,
            canEditBackground = canEditBackground,
        ),
        savedPlayers = tablet.savedProfiles.filter { it.profileId !in seated }.map { LobbySavedPlayer(it.profileId, it.name) },
        enabled = !tablet.busy,
        settingsEnabled = seat != null && !tablet.busy,
        canAdd = true,
        identityHint = "A new name gets an account without a PIN. Sigils can join too.",
        savedTitle = "Saved players",
        headerActions = {
            TextButton(onClick = onClose) { Text("Back", color = p.muted) }
            TextButton(onClick = { confirmLeave = true }) { Text("Leave tablet mode", color = p.bad) }
        },
        feedback = { tablet.message?.let { Feedback(it.message, it.isError) } },
    )
}

@Composable
private fun PinDialog(name: String, onSubmit: (String) -> Unit, onDismiss: () -> Unit) {
    val p = palette
    var pin by remember { mutableStateOf("") }
    AlertDialog(
        onDismissRequest = onDismiss,
        containerColor = p.surface,
        title = { Text("$name's PIN") },
        text = {
            Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
                Text("This player keeps a PIN for Sigils and tablets. They can turn that off in My Account.", color = p.muted)
                OutlinedTextField(
                    value = pin,
                    onValueChange = { pin = it.take(ProfileSecret.MAX_CHARS) },
                    label = { Text("PIN or password") },
                    singleLine = true,
                    visualTransformation = PasswordVisualTransformation(),
                    keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Password, imeAction = ImeAction.Done),
                    keyboardActions = KeyboardActions(onDone = { if (pin.isNotEmpty()) onSubmit(pin) }),
                    modifier = Modifier.fillMaxWidth(),
                )
            }
        },
        confirmButton = {
            TextButton(onClick = { onSubmit(pin) }, enabled = pin.isNotEmpty()) { Text("Seat player", fontWeight = FontWeight.Bold) }
        },
        dismissButton = { TextButton(onClick = onDismiss) { Text("Cancel", color = p.text) } },
    )
}
