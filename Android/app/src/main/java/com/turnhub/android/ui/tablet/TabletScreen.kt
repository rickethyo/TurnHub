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
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawingPadding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.selection.toggleable
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.FilterChip
import androidx.compose.material3.FilterChipDefaults
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.SuggestionChip
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalView
import androidx.compose.ui.semantics.LiveRegionMode
import androidx.compose.ui.semantics.liveRegion
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.heading
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
import com.turnhub.android.domain.ControllerHandle
import com.turnhub.android.domain.TablePlayer
import com.turnhub.android.domain.TableSummary
import com.turnhub.android.protocol.AccountPermission
import com.turnhub.android.protocol.GameProfile
import com.turnhub.android.protocol.SessionInfo
import com.turnhub.android.protocol.TableState
import com.turnhub.android.ui.components.AccentButton
import com.turnhub.android.ui.components.BrassCard
import com.turnhub.android.ui.components.OfflineBanner
import com.turnhub.android.ui.components.Eyebrow
import com.turnhub.android.ui.components.Tone
import com.turnhub.android.ui.components.ToneButton
import com.turnhub.android.ui.components.tableBackground
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
 * Tablet mode, as the portal's `/tablet`: this device lies in the middle of
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
) {
    val p = palette
    KeepScreenOn()
    BackHandler(onBack = onClose)
    tablet.pinPrompt?.let { prompt -> PinDialog(prompt.name, onSubmit = { pin -> actions.run { seatSaved(prompt.profileId, prompt.name, pin) } }, onDismiss = actions.onDismissPin) }
    val granted = signedIn && session?.tablet == true
    Box(modifier.fillMaxSize().tableBackground(p)) {
        when {
            summary == null -> Gate(tablet, actions, onClose) {
                Text("Connect to Atlas first, then open tablet mode again.", color = p.muted)
            }
            !granted -> Gate(tablet, actions, onClose) {
                GateSteps(signedIn, session?.has(AccountPermission.TABLET_ACCESS) == true, tablet, actions)
            }
            summary.state == TableState.LOBBY -> Lobby(summary, tablet, actions, onClose)
            else -> {
                Immersive()
                TabletTable(summary, tablet, { block -> actions.run { this.block() } }, actions.onDismissMessage, reduceMotion, onClose, offline)
            }
        }
        if (offline && summary != null) {
            val playing = granted && summary.state != TableState.LOBBY
            if (playing) OfflineStrip(tablet.waiting, Modifier.align(Alignment.TopCenter))
            else OfflineCover(summary, onClose)
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

/** Formats offered in the lobby, with the starting life each sets. */
internal val FORMATS = listOf(
    Triple(GameProfile.MTG_COMMANDER, "Commander", 40),
    Triple(GameProfile.MTG, "Magic", 20),
    Triple(GameProfile.YUGIOH, "Yu-Gi-Oh!", 8000),
    Triple(GameProfile.GENERIC, "Other", 20),
)

/** Any seated player carries lobby settings and Start; Atlas treats them alike. */
internal fun TableSummary.anySeat(): TabletSeat? = players.firstOrNull()?.seat()

internal fun TablePlayer.seat(): TabletSeat = TabletSeat(controller.id, slot)

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun Lobby(summary: TableSummary, tablet: TabletState, actions: TabletActions, onClose: () -> Unit) {
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
    Box(Modifier.fillMaxSize().safeDrawingPadding(), contentAlignment = Alignment.TopCenter) {
        Column(
            Modifier.widthIn(max = 640.dp).fillMaxWidth().verticalScroll(rememberScrollState()).padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(14.dp),
        ) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text("New game", color = p.text, style = MaterialTheme.typography.headlineSmall,
                    modifier = Modifier.weight(1f).semantics { heading() })
                TextButton(onClick = onClose) { Text("Back", color = p.muted) }
                TextButton(onClick = { confirmLeave = true }) { Text("Leave tablet mode", color = p.bad) }
            }
            tablet.message?.let { Feedback(it.message, it.isError) }

            BrassCard {
                Eyebrow("At the table")
                if (summary.players.isEmpty()) {
                    Text("Add everyone who is playing. Sigils can join too.", color = p.muted)
                }
                summary.players.forEach { player -> SeatedRow(player, summary.players.size, tablet.busy, actions) }
                if (summary.players.size > 1) {
                    Text("Arrows set the turn order; panels sit clockwise in this order.", color = p.faint,
                        style = MaterialTheme.typography.bodySmall)
                }
                var name by rememberSaveable { mutableStateOf("") }
                val add = {
                    val trimmed = name.trim()
                    if (trimmed.isNotEmpty()) {
                        actions.run { seatNew(trimmed) }
                        name = ""
                    }
                }
                Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    OutlinedTextField(
                        value = name,
                        onValueChange = { name = it.take(32) },
                        label = { Text("New player's name") },
                        singleLine = true,
                        keyboardOptions = KeyboardOptions(imeAction = ImeAction.Done),
                        keyboardActions = KeyboardActions(onDone = { add() }),
                        modifier = Modifier.weight(1f),
                    )
                    ToneButton("Add", add, enabled = name.isNotBlank() && !tablet.busy)
                }
                Text("A new name gets an account without a PIN.", color = p.faint, style = MaterialTheme.typography.bodySmall)
                val seated = summary.players.mapNotNull { it.profileId }.toSet()
                val free = tablet.savedProfiles.filter { it.profileId !in seated }
                if (free.isNotEmpty()) {
                    Text("Saved players", color = p.muted, style = MaterialTheme.typography.labelLarge)
                    FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
                        free.forEach { profile ->
                            SuggestionChip(
                                onClick = { actions.run { seatSaved(profile.profileId, profile.name) } },
                                label = { Text(profile.name.ifBlank { "Unnamed" }) },
                                enabled = !tablet.busy,
                            )
                        }
                    }
                }
            }

            GameCard(summary, tablet, actions)

            AccentButton(
                if (summary.players.size < 2) "Seat two players to start" else "Start the game",
                { summary.anySeat()?.let { seat -> actions.run { control(seat, "start") } } },
                Modifier.fillMaxWidth(),
                enabled = summary.players.size >= 2 && !tablet.busy,
            )
        }
    }
}

@Composable
private fun SeatedRow(player: TablePlayer, count: Int, busy: Boolean, actions: TabletActions) {
    val p = palette
    Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(10.dp)) {
        Box(
            Modifier.size(30.dp).background(p.avatarColor(player.playerNumber), CircleShape),
            contentAlignment = Alignment.Center,
        ) {
            Text("${player.playerNumber}", color = p.avatarText, style = MaterialTheme.typography.labelLarge)
        }
        Text(player.label, color = p.text, style = MaterialTheme.typography.bodyLarge, modifier = Modifier.weight(1f))
        if (player.controller.kind == ControllerHandle.Kind.PHYSICAL) {
            Text(
                "Sigil",
                color = p.muted,
                style = MaterialTheme.typography.labelMedium,
                modifier = Modifier.border(1.dp, p.line, RoundedCornerShape(6.dp)).padding(horizontal = 6.dp, vertical = 2.dp),
            )
        }
        MoveButton("↑", "Move ${player.label} earlier in turn order", !busy && player.playerNumber > 1) {
            actions.run { control(player.seat(), "move-earlier") }
        }
        MoveButton("↓", "Move ${player.label} later in turn order", !busy && player.playerNumber < count) {
            actions.run { control(player.seat(), "move-later") }
        }
        player.profileId?.let { id ->
            IconButton(onClick = { actions.run { unseat(id) } }) {
                Text("×", color = p.muted, style = MaterialTheme.typography.titleLarge,
                    modifier = Modifier.semantics { contentDescription = "Remove ${player.label}" })
            }
        }
    }
}

@Composable
internal fun MoveButton(glyph: String, label: String, enabled: Boolean, onClick: () -> Unit) {
    val p = palette
    IconButton(onClick = onClick, enabled = enabled, modifier = Modifier.semantics { contentDescription = label }) {
        Text(glyph, color = if (enabled) p.text else p.faint, style = MaterialTheme.typography.titleLarge)
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun GameCard(summary: TableSummary, tablet: TabletState, actions: TabletActions) {
    val p = palette
    val settings = summary.settings
    val seat = summary.anySeat()
    BrassCard {
        Eyebrow("Game")
        if (seat == null) Text("Add a player to choose the game.", color = p.muted)
        FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            FORMATS.forEach { (profile, label, life) ->
                FilterChip(
                    selected = settings.profile == profile,
                    onClick = { seat?.let { actions.run { saveSettings(it, profile.wireValue, life, null) } } },
                    label = { Text(label) },
                    enabled = seat != null && !tablet.busy,
                    colors = FilterChipDefaults.filterChipColors(selectedContainerColor = p.accentSoft),
                )
            }
        }
        var life by rememberSaveable(settings.startingLife) { mutableStateOf("${settings.startingLife}") }
        Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            OutlinedTextField(
                value = life,
                onValueChange = { life = it.filter(Char::isDigit).take(5) },
                label = { Text(if (settings.twoHeadedGiant) "Starting life per team" else "Starting life") },
                singleLine = true,
                keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number),
                modifier = Modifier.weight(1f),
            )
            ToneButton(
                "Set",
                { seat?.let { s -> life.toIntOrNull()?.let { actions.run { saveSettings(s, null, it, null) } } } },
                enabled = seat != null && life.toIntOrNull() != null && life != "${settings.startingLife}" && !tablet.busy,
            )
        }
        if (settings.profile == GameProfile.MTG || settings.profile == GameProfile.MTG_COMMANDER) {
            Row(
                Modifier.fillMaxWidth().toggleable(
                    value = settings.twoHeadedGiant,
                    enabled = seat != null && !tablet.busy,
                    role = Role.Switch,
                    onValueChange = { on -> seat?.let { actions.run { saveSettings(it, null, null, on) } } },
                ),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Column(Modifier.weight(1f)) {
                    Text("Two-Headed Giant", color = p.text)
                    Text("Neighbours in turn order play as teams of two.", color = p.muted, style = MaterialTheme.typography.bodySmall)
                }
                Switch(checked = settings.twoHeadedGiant, onCheckedChange = null)
            }
        }
    }
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
