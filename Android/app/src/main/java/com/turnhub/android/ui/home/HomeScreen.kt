package com.turnhub.android.ui.home

import androidx.annotation.DrawableRes
import androidx.compose.animation.AnimatedContent
import androidx.compose.animation.EnterTransition
import androidx.compose.animation.ExitTransition
import androidx.compose.animation.core.tween
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.slideInVertically
import androidx.compose.animation.togetherWith
import androidx.compose.material3.Icon
import androidx.compose.ui.draw.drawBehind
import com.turnhub.android.ui.components.rememberHaptics
import androidx.compose.ui.res.painterResource
import com.turnhub.android.ui.theme.DesignTokens
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawing
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.statusBarsPadding
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.layout.windowInsetsPadding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Badge
import androidx.compose.material3.BadgedBox
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.NavigationBar
import androidx.compose.material3.NavigationBarItem
import androidx.compose.material3.NavigationBarItemDefaults
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.semantics.LiveRegionMode
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.clearAndSetSemantics
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.liveRegion
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.turnhub.android.protocol.AtlasConnectionState
import com.turnhub.android.protocol.LedStyle
import com.turnhub.android.protocol.LifeRequestState
import com.turnhub.android.protocol.ProfileSummary
import com.turnhub.android.protocol.TableState
import com.turnhub.android.ui.components.AccentButton
import com.turnhub.android.ui.components.BrassCard
import com.turnhub.android.ui.components.Eyebrow
import com.turnhub.android.ui.components.GearMark
import com.turnhub.android.ui.components.ToneButton
import com.turnhub.android.ui.components.Tone
import com.turnhub.android.ui.components.rememberNowMs
import com.turnhub.android.ui.components.tableBackground
import com.turnhub.android.ui.theme.TurnHubThemeChoice
import com.turnhub.android.ui.theme.palette
import com.turnhub.android.ui.setup.SetupActions
import com.turnhub.android.ui.setup.SetupScreen
import com.turnhub.android.ui.manual.ManualScreen

private enum class HomeTab(val label: String, @DrawableRes val icon: Int) {
    GAME("Game", DesignTokens.Icons.timer),
    PLAYERS("Players", DesignTokens.Icons.users),
    ACCOUNT("Me", DesignTokens.Icons.user),
    SETTINGS("Settings", DesignTokens.Icons.sliders),
    DEV("Developer", DesignTokens.Icons.signal),
}

/**
 * The app: a connect screen until Atlas answers, then the portal's Game,
 * Players and My Account tabs. A pure function of [uiState] plus callbacks;
 * Atlas decides every outcome.
 */
@Composable
fun HomeScreen(
    uiState: HomeUiState,
    onEndpointChange: (String) -> Unit,
    onConnectClick: () -> Unit,
    onDisconnectClick: () -> Unit,
    onOpenAppSettings: () -> Unit,
    onWifiPasswordSubmit: (ssid: String, passphrase: String) -> Unit,
    onUseCurrentWifi: () -> Unit,
    onWifiPromptDismiss: () -> Unit,
    modifier: Modifier = Modifier,
    discoveryActions: DiscoveryActions = DiscoveryActions(),
    gameActions: GameActions = GameActions(),
    accountActions: AccountActions = AccountActions(),
    theme: TurnHubThemeChoice = TurnHubThemeChoice.AUTO,
    reduceMotion: Boolean = false,
    onSignInSubmit: (ProfileSummary, String, Boolean) -> Unit = { _, _, _ -> },
    onSignInDismiss: () -> Unit = {},
    onCreateAccount: (name: String, pin: String, remember: Boolean) -> Unit = { _, _, _ -> },
    onAccessibilitySave: (sigilSound: Boolean, ledStyle: LedStyle, longPressMs: Int, winHoldMs: Int, lifeApprovalMs: Int) -> Unit =
        { _, _, _, _, _ -> },
    onAccessibilityDismiss: () -> Unit = {},
    admin: com.turnhub.android.data.AdminState = com.turnhub.android.data.AdminState(),
    adminActions: AdminActions = AdminActions(),
    setup: com.turnhub.android.data.SetupState = com.turnhub.android.data.SetupState(),
    setupActions: SetupActions = SetupActions(),
    updatesAvailable: Int = 0,
    onOpenUpdates: () -> Unit = {},
    seatClaim: com.turnhub.android.data.SeatClaim? = null,
    seatActions: SeatActions = SeatActions(),
    tablet: com.turnhub.android.data.TabletState = com.turnhub.android.data.TabletState(),
    tabletActions: com.turnhub.android.ui.tablet.TabletActions = com.turnhub.android.ui.tablet.TabletActions(),
) {
    PresenceCodeDialog(admin, adminActions)
    uiState.wifiPrompt?.let { prompt ->
        WifiPasswordDialog(prompt = prompt, onSubmit = onWifiPasswordSubmit, onUseCurrentWifi = onUseCurrentWifi, onDismiss = onWifiPromptDismiss)
    }
    uiState.signIn?.let { prompt -> SignInDialog(prompt = prompt, onSubmit = onSignInSubmit, onCreate = onCreateAccount, onDismiss = onSignInDismiss) }
    uiState.accessibility?.let { prompt ->
        AccessibilityDialog(prompt = prompt, onSave = onAccessibilitySave, onDismiss = onAccessibilityDismiss)
    }
    val p = palette
    val haptics = rememberHaptics()
    val summary = uiState.tableSummary
    var tab by rememberSaveable { mutableStateOf(HomeTab.GAME) }
    val me = uiState.me()
    val info = uiState.sessionInfo()
    val canSettings = info?.has(com.turnhub.android.protocol.AccountPermission.ADMIN) == true
    val canPeople = canSettings || info?.has(com.turnhub.android.protocol.AccountPermission.GAME_MASTER) == true
    val canDev = info?.has(com.turnhub.android.protocol.AccountPermission.DEVELOPER) == true
    val tabs = HomeTab.entries.filter {
        when (it) {
            HomeTab.SETTINGS -> canSettings
            HomeTab.DEV -> canDev
            else -> true
        }
    }
    if (tab !in tabs) tab = HomeTab.GAME
    // Presence also says whether Atlas still needs its first Admin.
    androidx.compose.runtime.LaunchedEffect(info?.profileId) { if (info != null) adminActions.onRefresh() }
    val incomingRequest = me?.lifeRequest?.let { it.state == LifeRequestState.PENDING && it.target == me.playerNumber } == true
    // The bundled user manual, reachable from the top bar before or after connecting.
    var showManual by rememberSaveable { mutableStateOf(false) }
    if (showManual) {
        ManualScreen(onClose = { showManual = false }, modifier = modifier)
        return
    }
    // Tablet mode: this device becomes the table's shared screen until closed.
    var showTablet by rememberSaveable { mutableStateOf(false) }
    if (showTablet) {
        com.turnhub.android.ui.tablet.TabletScreen(
            summary = summary,
            session = info,
            signedIn = uiState.player?.signedIn == true,
            tablet = tablet,
            actions = tabletActions,
            reduceMotion = reduceMotion,
            onClose = { showTablet = false },
            modifier = modifier,
        )
        return
    }

    Scaffold(
        modifier = modifier.tableBackground(p),
        containerColor = Color.Transparent,
        topBar = {
            BrandBar(uiState, running = summary?.state == TableState.RUNNING, reduceMotion = reduceMotion,
                onManualClick = { showManual = true })
        },
        bottomBar = {
            if (summary != null && !setup.visible) {
                // A translucent bar over the content with a hairline top edge,
                // drawn behind the gesture area (edge-to-edge).
                NavigationBar(
                    containerColor = if (p.ornament) p.surface else p.surface.copy(alpha = .94f),
                    tonalElevation = 0.dp,
                    modifier = Modifier.drawBehind {
                        drawLine(p.line, Offset(0f, 0f), Offset(size.width, 0f), 1.dp.toPx())
                    },
                ) {
                    tabs.forEach { entry ->
                        val selected = tab == entry
                        NavigationBarItem(
                            selected = selected,
                            onClick = {
                                if (!selected) haptics.tick()
                                tab = entry
                            },
                            icon = {
                                BadgedBox(badge = { if (entry == HomeTab.GAME && incomingRequest) Badge { Text("!") } }) {
                                    Icon(painterResource(entry.icon), contentDescription = null, modifier = Modifier.size(24.dp))
                                }
                            },
                            label = {
                                Text(
                                    entry.label,
                                    style = MaterialTheme.typography.labelMedium,
                                    fontWeight = if (selected) FontWeight.SemiBold else FontWeight.Medium,
                                )
                            },
                            colors = NavigationBarItemDefaults.colors(
                                selectedIconColor = if (p.dark) p.accentHi else p.accent,
                                selectedTextColor = if (p.dark) p.accentHi else p.accent,
                                unselectedIconColor = p.muted,
                                unselectedTextColor = p.muted,
                                indicatorColor = if (p.ornament) p.surface3 else p.accentSoft,
                            ),
                        )
                    }
                }
            }
        },
    ) { innerPadding ->
        val nowMs = rememberNowMs(
            ticking = summary != null &&
                (summary.state == TableState.RUNNING || summary.pending.passPlayer != null ||
                    summary.players.any { it.lifeRequest?.state == LifeRequestState.PENDING }),
        )
        val labels = summary?.players?.associate { it.playerNumber to it.label }.orEmpty()
        val labelFor: (Int) -> String = { number -> labels[number] ?: "Player $number" }
        Box(
            Modifier
                .fillMaxSize()
                .padding(innerPadding),
            contentAlignment = Alignment.TopCenter,
        ) {
            Column(
                Modifier
                    .widthIn(max = 720.dp)
                    .fillMaxWidth()
                    .verticalScroll(rememberScrollState())
                    .padding(horizontal = 16.dp, vertical = 12.dp),
                verticalArrangement = Arrangement.spacedBy(14.dp),
            ) {
                // Atlas dropping out is expected while an update installs; the update step explains it.
                val installing = setup.visible && setup.updates is com.turnhub.android.data.UpdatesState.Installing
                uiState.errorMessage?.takeUnless { installing }?.let {
                    ErrorCard(it, uiState.errorDetail, uiState.isRetrying, onOpenAppSettings.takeIf { uiState.offerAppSettings })
                }
                if (setup.visible) {
                    SetupScreen(setup, setupActions)
                } else if (summary == null) {
                    ConnectCard(uiState, onEndpointChange, onConnectClick, onDisconnectClick, discoveryActions, reduceMotion)
                } else {
                    if (updatesAvailable > 0) UpdateAvailableCard(updatesAvailable, onOpenUpdates)
                    // A short fade and rise between tabs; none when motion is reduced.
                    AnimatedContent(
                        targetState = tab,
                        transitionSpec = {
                            if (reduceMotion) {
                                EnterTransition.None togetherWith ExitTransition.None
                            } else {
                                (fadeIn(tween(DesignTokens.Motion.BASE_MS)) +
                                    slideInVertically(tween(DesignTokens.Motion.BASE_MS)) { it / 40 }) togetherWith
                                    fadeOut(tween(DesignTokens.Motion.INSTANT_MS))
                            }
                        },
                        label = "tab",
                    ) { shown ->
                        Column(verticalArrangement = Arrangement.spacedBy(14.dp)) {
                            when (shown) {
                                HomeTab.GAME -> GameTab(uiState, summary, nowMs, reduceMotion, gameActions, labelFor)
                                HomeTab.PLAYERS -> PlayersTab(summary, me?.playerNumber, nowMs, labelFor, uiState.endpointText, seatClaim, seatActions) {
                                    if (canPeople) PeopleCard(info, admin, uiState.avatars, adminActions)
                                }
                                HomeTab.ACCOUNT -> {
                                    if (admin.presence?.setup == true) AdminSetupCard(adminActions)
                                    AccountTab(uiState, theme, reduceMotion, accountActions.copy(onDisconnect = onDisconnectClick, onOpenTablet = { showTablet = true }))
                                }
                                HomeTab.SETTINGS -> info?.let { SettingsTab(it, admin, adminActions, onShowPeople = { tab = HomeTab.PLAYERS }) }
                                HomeTab.DEV -> DevTab(admin, adminActions)
                            }
                        }
                    }
                }
                Text(
                    "TurnHub runs locally on Atlas. No cloud connection is required for table control.",
                    color = p.faint,
                    style = MaterialTheme.typography.bodySmall,
                    textAlign = TextAlign.Center,
                    modifier = Modifier.fillMaxWidth().padding(vertical = 12.dp),
                )
            }
        }
    }
}

@Composable
private fun BrandBar(uiState: HomeUiState, running: Boolean, reduceMotion: Boolean, onManualClick: () -> Unit) {
    val p = palette
    Row(
        Modifier
            .fillMaxWidth()
            .background(p.bg.copy(alpha = .92f))
            .statusBarsPadding()
            .padding(horizontal = 16.dp, vertical = 10.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(11.dp),
    ) {
        GearMark(if (p.ornament) 34.dp else 28.dp, spinning = running, reduceMotion = reduceMotion)
        Column(Modifier.weight(1f)) {
            Text("TurnHub", color = p.text, style = MaterialTheme.typography.headlineSmall.copy(fontSize = 22.sp),
                modifier = Modifier.semantics { heading() })
            if (p.ornament) {
                Text("ATLAS TABLE CONSOLE", color = p.muted, style = MaterialTheme.typography.labelSmall.copy(letterSpacing = 2.sp))
            }
        }
        ConnectionPill(uiState)
        ManualButton(onManualClick)
    }
}

/**
 * "Update available" (the Atlas screen and Sigils say the same), with the way
 * to install it. Words first: the card never relies on color.
 */
@Composable
private fun UpdateAvailableCard(devices: Int, onOpenUpdates: () -> Unit) {
    val p = palette
    BrassCard(highlight = p.accent) {
        Eyebrow("Update available")
        Text(
            if (devices == 1) "Newer firmware is ready for 1 device at this table."
            else "Newer firmware is ready for $devices devices at this table.",
            color = p.text,
            modifier = Modifier.semantics { liveRegion = LiveRegionMode.Polite },
        )
        AccentButton("Update now", onOpenUpdates, Modifier.fillMaxWidth())
    }
}

/** A round "?" that opens the user manual; TalkBack reads it as "User manual". */
@Composable
private fun ManualButton(onClick: () -> Unit) {
    val p = palette
    Box(
        Modifier
            .size(48.dp)
            .clip(CircleShape)
            .clickable(role = Role.Button, onClick = onClick)
            .semantics { contentDescription = "User manual" },
        contentAlignment = Alignment.Center,
    ) {
        Box(
            Modifier
                .size(32.dp)
                .clip(CircleShape)
                .background(p.inset)
                .border(1.dp, p.line, CircleShape),
            contentAlignment = Alignment.Center,
        ) {
            Text("?", color = if (p.dark) p.accentHi else p.accent,
                style = MaterialTheme.typography.titleMedium.copy(fontWeight = FontWeight.Bold),
                modifier = Modifier.clearAndSetSemantics { })
        }
    }
}

@Composable
private fun ConnectionPill(uiState: HomeUiState) {
    val p = palette
    val (text, color) = when {
        uiState.joiningSsid != null -> "Joining Wi-Fi" to p.warn
        uiState.isRetrying -> "Retrying" to p.warn
        uiState.connectionState == AtlasConnectionState.CONNECTED -> "Connected" to p.good
        uiState.connectionState == AtlasConnectionState.CONNECTING -> "Connecting" to p.warn
        else -> "Offline" to p.bad
    }
    val pill = RoundedCornerShape(99.dp)
    Row(
        Modifier
            .clip(pill)
            .then(
                if (p.ornament || p.bg == Color.Black) Modifier.background(p.inset).border(1.dp, color.copy(alpha = .5f), pill)
                else Modifier.background(color.copy(alpha = .14f)),
            )
            .padding(horizontal = 10.dp, vertical = 6.dp)
            .semantics(mergeDescendants = true) { liveRegion = LiveRegionMode.Polite },
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(6.dp),
    ) {
        Box(Modifier.size(8.dp).clip(CircleShape).background(color))
        Text(text, color = color, style = MaterialTheme.typography.labelMedium.copy(fontWeight = FontWeight.Bold))
    }
}

/** The connect screen's retry and new-table actions. */
data class DiscoveryActions(
    val onSearchAgain: () -> Unit = {},
    val onSetUpNewTable: () -> Unit = {},
)

/**
 * Before a table answers: one heading, one status line and one main action.
 * On launch the app rejoins the saved table by itself
 * (HomeViewModel.onAppStarted), so most of the time this only says what it's
 * doing; a phone with no saved table leads with setup. The address is for
 * unusual setups and stays under Advanced.
 */
@Composable
private fun ConnectCard(
    uiState: HomeUiState,
    onEndpointChange: (String) -> Unit,
    onConnectClick: () -> Unit,
    onDisconnectClick: () -> Unit,
    discovery: DiscoveryActions,
    reduceMotion: Boolean,
) {
    val p = palette
    val working = uiState.rejoining || uiState.joiningSsid != null ||
        uiState.connectionState == AtlasConnectionState.CONNECTING || uiState.discovery == Discovery.Searching
    val (title, status) = when {
        uiState.rejoining -> "Atlas is restarting" to
            "Rejoining the table's Wi-Fi. If Android asks to connect, choose Connect."
        uiState.joiningSsid != null -> "Joining your table" to
            "Joining ${uiState.joiningSsid}. If Android asks to connect, choose Connect."
        uiState.connectionState == AtlasConnectionState.CONNECTING -> "Joining your table" to "Connecting to Atlas…"
        uiState.discovery == Discovery.Searching -> "Looking for your table" to "Rejoining your table's Wi-Fi…"
        uiState.discovery == Discovery.NotFound -> "Couldn't reach your table" to
            "Check that Atlas is switched on and close by. If it was factory reset, set it up again."
        !uiState.hasSavedTable -> "Welcome to TurnHub" to
            "Setting up a new table takes a few minutes: an account, a code from the Atlas screen, your " +
            "Sigils and a Wi-Fi password. Joining a table someone already set up? Connect to it instead."
        else -> "Join your table" to "TurnHub joins the Atlas Wi-Fi for you, then shows the live table."
    }
    BrassCard {
        Column(Modifier.fillMaxWidth(), horizontalAlignment = Alignment.CenterHorizontally, verticalArrangement = Arrangement.spacedBy(10.dp)) {
            GearMark(96.dp, spinning = working, reduceMotion = reduceMotion)
            Text(title, style = MaterialTheme.typography.headlineMedium, color = p.text, textAlign = TextAlign.Center,
                modifier = Modifier.semantics { heading() })
            Text(
                status,
                color = p.muted,
                textAlign = TextAlign.Center,
                modifier = Modifier.semantics { liveRegion = LiveRegionMode.Polite },
            )
        }
        when {
            working -> Unit
            uiState.connectionState == AtlasConnectionState.CONNECTED ->
                ToneButton("Disconnect", onDisconnectClick, Modifier.fillMaxWidth())
            uiState.discovery == Discovery.NotFound -> {
                AccentButton("Try again", discovery.onSearchAgain, Modifier.fillMaxWidth().height(56.dp))
                ToneButton("Set up a new table", discovery.onSetUpNewTable, Modifier.fillMaxWidth())
            }
            !uiState.hasSavedTable -> {
                AccentButton("Set up a new table", discovery.onSetUpNewTable, Modifier.fillMaxWidth().height(56.dp))
                ToneButton("Connect to a table", onConnectClick, Modifier.fillMaxWidth())
            }
            else -> AccentButton("Connect to Atlas", onConnectClick, Modifier.fillMaxWidth().height(56.dp))
        }
        var advanced by rememberSaveable { mutableStateOf(false) }
        TextButton(onClick = { advanced = !advanced }, modifier = Modifier.fillMaxWidth()) {
            Text(if (advanced) "Hide Atlas address" else "Advanced: Atlas address", color = p.muted)
        }
        if (advanced) {
            OutlinedTextField(
                value = uiState.endpointText,
                onValueChange = onEndpointChange,
                enabled = uiState.endpointEditable,
                singleLine = true,
                label = { Text("Atlas address") },
                supportingText = { Text("Leave it at http://192.168.4.1 unless Atlas joined another network.") },
                keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Uri, imeAction = ImeAction.Go),
                keyboardActions = KeyboardActions(onGo = { onConnectClick() }),
                modifier = Modifier.fillMaxWidth(),
            )
        }
    }
}

@Composable
private fun ErrorCard(message: String, detail: String?, retrying: Boolean, onOpenAppSettings: (() -> Unit)?) {
    val p = palette
    Row(
        Modifier
            .fillMaxWidth()
            .clip(RoundedCornerShape(12.dp))
            .background(p.surface2)
            .border(1.dp, p.line, RoundedCornerShape(12.dp))
            .semantics(mergeDescendants = true) { liveRegion = LiveRegionMode.Polite },
    ) {
        Column(
            Modifier
                .padding(14.dp)
                .weight(1f),
            verticalArrangement = Arrangement.spacedBy(4.dp),
        ) {
            Text(
                if (retrying) "Atlas not responding – retrying" else "Problem",
                color = if (retrying) p.warn else p.bad,
                style = MaterialTheme.typography.titleSmall,
            )
            Text(message, color = p.text, style = MaterialTheme.typography.bodyMedium)
            detail?.let { Text(it, color = p.faint, style = MaterialTheme.typography.bodySmall) }
            onOpenAppSettings?.let { ToneButton("Open app settings", it, tone = Tone.INFO) }
        }
    }
}
