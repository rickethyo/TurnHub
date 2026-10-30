package com.turnhub.android.ui.home

import androidx.annotation.DrawableRes
import androidx.compose.animation.AnimatedContent
import androidx.compose.animation.EnterTransition
import androidx.compose.animation.ExitTransition
import androidx.compose.animation.core.tween
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.slideInHorizontally
import androidx.compose.animation.slideOutHorizontally
import androidx.compose.animation.togetherWith
import androidx.compose.material3.Icon
import androidx.compose.ui.draw.drawBehind
import com.turnhub.android.ui.components.AtlasBatteryGauge
import com.turnhub.android.ui.components.rememberHaptics
import androidx.compose.ui.res.painterResource
import com.turnhub.android.ui.theme.DesignTokens
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.statusBarsPadding
import androidx.compose.foundation.layout.widthIn
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
    TABLE("Table", DesignTokens.Icons.table),
    /** Planechase for the device game; shown only while a Magic format is chosen. */
    PLANES("Planes", DesignTokens.Icons.cards),
    /** Death May Die's Elder One, episode and Story board; shown only while that game is chosen. */
    ELDER("Elder One", DesignTokens.Icons.eye),
    PLAYERS("Players", DesignTokens.Icons.users),
    HISTORY("History", DesignTokens.Icons.clock),
    MORE("More", DesignTokens.Icons.more),
}

/**
 * One navigation shell for device play and Atlas capabilities.
 * Each authority retains its own game state and action callbacks.
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
    /** Wi-Fi is off: open Android's Wi-Fi switch, then the connection carries on. */
    onTurnOnWifi: () -> Unit = {},
    onWifiOffDismiss: () -> Unit = {},
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
    onFollowGame: (Int) -> Unit = {},
    onAtlasOpened: () -> Unit = {},
    /**
     * The device game's Game page around [running], the shared Game tab drawn
     * for it once it starts; the lobby before. openTable switches to the
     * Table tab, openPlanes to the Planes tab.
     */
    localGameContent: @Composable (openTable: () -> Unit, openPlanes: () -> Unit, running: @Composable () -> Unit) -> Unit = { _, _, _ -> },
    /** Plays the device game full screen. */
    onPlayOnDevice: () -> Unit = {},
    /** The device game's table, in the Table tab; onSetUp returns to Game. */
    localTableContent: @Composable (onSetUp: () -> Unit, onOpenPlanes: () -> Unit) -> Unit = { _, _ -> },
    requestedTab: String? = null,
    onTabRequestHandled: () -> Unit = {},
    localPlayersContent: @Composable () -> Unit = {},
    localHistoryContent: @Composable () -> Unit = {},
    /** The device game's Planechase setup and planes, in the Planes tab. */
    localPlanesContent: @Composable () -> Unit = {},
    /** The device game's Elder One and Story board (Death May Die), in the Elder One tab. */
    localElderContent: @Composable () -> Unit = {},
    atlasEnabled: Boolean = true,
    onAtlasEnabled: (Boolean) -> Unit = {},
    localState: com.turnhub.android.standalone.StandaloneState = com.turnhub.android.standalone.StandaloneState(),
    localTable: com.turnhub.android.standalone.StandaloneTable? = null,
    onOpenSharedGame: (() -> Unit)? = null,
    sharedGameHosting: Boolean = false,
    sharedGameCode: String? = null,
) {
    PresenceCodeDialog(admin, adminActions)
    uiState.wifiPrompt?.let { prompt ->
        WifiPasswordDialog(prompt = prompt, onSubmit = onWifiPasswordSubmit, onUseCurrentWifi = onUseCurrentWifi, onDismiss = onWifiPromptDismiss)
    }
    if (uiState.wifiOff) {
        androidx.compose.material3.AlertDialog(
            onDismissRequest = onWifiOffDismiss,
            title = { Text("Turn on Wi-Fi to connect?") },
            text = { Text("Wi-Fi is off on this phone. TurnHub reaches your Atlas table over its Wi-Fi.") },
            confirmButton = { androidx.compose.material3.TextButton(onClick = onTurnOnWifi) { Text("Turn on Wi-Fi") } },
            dismissButton = { androidx.compose.material3.TextButton(onClick = onWifiOffDismiss) { Text("Not now") } },
        )
    }
    uiState.signIn?.let { prompt -> SignInDialog(prompt = prompt, onSubmit = onSignInSubmit, onCreate = onCreateAccount, onDismiss = onSignInDismiss) }
    uiState.accessibility?.let { prompt ->
        AccessibilityDialog(prompt = prompt, onSave = onAccessibilitySave, onDismiss = onAccessibilityDismiss)
    }
    val p = palette
    val haptics = rememberHaptics()
    val summary = uiState.tableSummary?.let { localState.library.decorateTable(it) }
    var editingBackgroundId by rememberSaveable { mutableStateOf<String?>(null) }
    if (localTable != null) localState.library.players.firstOrNull { it.localId == editingBackgroundId }?.let { player ->
        com.turnhub.android.ui.tablet.LocalTileBackgroundDialog(player, localTable) { editingBackgroundId = null }
    }
    var tab by rememberSaveable { mutableStateOf(HomeTab.GAME) }
    var morePage by rememberSaveable { mutableStateOf(MorePage.HOME) }
    androidx.compose.runtime.LaunchedEffect(requestedTab) {
        HomeTab.entries.firstOrNull { it.name == requestedTab }?.let { tab = it; onTabRequestHandled() }
    }
    val me = uiState.me()
    val info = uiState.sessionInfo()
    val canSettings = info?.has(com.turnhub.android.protocol.AccountPermission.ADMIN) == true
    val canPeople = canSettings || info?.has(com.turnhub.android.protocol.AccountPermission.GAME_MASTER) == true
    val canDev = info?.has(com.turnhub.android.protocol.AccountPermission.DEVELOPER) == true
    // One Game tab: Atlas's game or this device's, never both side by side.
    var chosenSource by rememberSaveable { mutableStateOf<GameSource?>(null) }
    // A finished device game keeps the tab until the next one is set up, so its results stay in view.
    val deviceUnderway = localState.game.state != TableState.LOBBY
    val source = gameSource(atlasTable = summary != null, seatedAtAtlas = me != null,
        deviceGameUnderway = deviceUnderway, chosen = chosenSource)
    val atlasGame = source == GameSource.ATLAS
    val deviceSummary = androidx.compose.runtime.remember(localState.game, localState.library.players) {
        localState.game.toSummary(System.currentTimeMillis(), com.turnhub.android.domain.TableClock.nowMs(),
            localState.library.players.associate { it.localId to it.personalization })
    }
    val deviceGameActions = GameActions(
        onOpenTablet = onPlayOnDevice,
        onEliminationRule = { field, value -> localTable?.changeEliminationRule(field, value) },
    )
    // The Planes tab shows only while Planechase is on (Game setup's toggle turns it on).
    val planes = localState.game.profile.magicRules && localState.game.planechase.enabled
    // Atlas doesn't run Death May Die, so the tab is for this device's own game only.
    val elderOne = localState.game.profile.deathMayDie && !atlasGame
    val tabs = HomeTab.entries.filter { (it != HomeTab.PLANES || planes) && (it != HomeTab.ELDER || elderOne) }
    // Leaving Magic or turning Planechase off hides the Planes tab (Death May Die the Elder One tab); whoever was on it goes back to Game.
    androidx.compose.runtime.LaunchedEffect(planes) { if (!planes && tab == HomeTab.PLANES) tab = HomeTab.GAME }
    androidx.compose.runtime.LaunchedEffect(elderOne) { if (!elderOne && tab == HomeTab.ELDER) tab = HomeTab.GAME }
    var playersPage by rememberSaveable { mutableStateOf("Device") }
    val pageState = androidx.compose.runtime.saveable.rememberSaveableStateHolder()
    androidx.activity.compose.BackHandler(enabled = tab != HomeTab.GAME && !setup.visible) {
        if (tab == HomeTab.MORE && morePage != MorePage.HOME) morePage = MorePage.HOME
        else tab = HomeTab.GAME
    }
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
    com.turnhub.android.ui.components.GameSoundEffect(summary, available = !uiState.isOffline)
    var showTablet by rememberSaveable { mutableStateOf(false) }
    if (showTablet || (tab == HomeTab.TABLE && atlasGame && !setup.visible)) {
        com.turnhub.android.ui.tablet.TabletScreen(
            summary = summary,
            session = info,
            signedIn = uiState.player?.signedIn == true,
            tablet = tablet,
            actions = tabletActions,
            reduceMotion = reduceMotion,
            offline = uiState.isOffline,
            onFollowGame = onFollowGame,
            switching = uiState.player?.busy == true,
            onClose = {
                showTablet = false
                tab = HomeTab.GAME
                if (info?.tablet == true) tabletActions.run { disable() }
            },
            onBackToSetup = { showTablet = false; tab = HomeTab.GAME },
            modifier = modifier,
            onEditBackground = if (localTable == null) null else { player ->
                editingBackgroundId = summary?.let { localState.library.linkedPlayer(it.atlasId, player.profileId)?.localId }
            },
            canEditBackground = { player -> summary?.let { localState.library.linkedPlayer(it.atlasId, player.profileId) } != null },
        )
        return
    }

    // A saved/background Atlas connection must not label a device game as Atlas play.
    val reportFromAtlas = summary != null && (setup.visible ||
        (tab in listOf(HomeTab.GAME, HomeTab.TABLE) && atlasGame) ||
        (tab == HomeTab.PLAYERS && playersPage != "Device"))
    val reportContext = if (reportFromAtlas && summary != null) {
        com.turnhub.android.ui.components.tableBugReportContext(summary,
            if (setup.visible) "Atlas setup" else tab.label)
    } else {
        val deviceGame = tab != HomeTab.MORE && !setup.visible
        com.turnhub.android.ui.components.BugReportContext(
            mode = if (deviceGame) com.turnhub.android.ui.components.BugReportMode.ON_DEVICE else null,
            screen = if (tab == HomeTab.MORE) morePage.title else tab.label,
            gameFormat = localState.game.profile.wireValue.takeIf { deviceGame },
            gameState = localState.game.state.name.takeIf { deviceGame },
        )
    }

    Scaffold(
        modifier = modifier.tableBackground(p),
        containerColor = Color.Transparent,
        topBar = {
            Column(Modifier.background(p.bg.copy(alpha = .92f))) {
                BrandBar(uiState, running = summary?.state == TableState.RUNNING, reduceMotion = reduceMotion,
                    report = reportContext,
                    onManualClick = { showManual = true })
                // Atlas games too: other phones can join through this one instead of each connecting to Atlas.
                if (onOpenSharedGame != null && !setup.visible && tab == HomeTab.GAME)
                    TextButton(onClick = onOpenSharedGame) { Text(when {
                        sharedGameCode != null -> "Play together · Code ${sharedGameCode.uppercase()}"
                        sharedGameHosting -> "Play together · Hosting"
                        else -> "Play together · Host or join"
                    }) }
            }
        },
        bottomBar = {
            if (!setup.visible) {
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
                                if (entry == HomeTab.MORE) morePage = MorePage.HOME
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
                                    maxLines = 1,
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
        val shownGame = if (atlasGame) summary else deviceSummary
        val nowMs = rememberNowMs(
            ticking = shownGame != null &&
                (shownGame.state == TableState.RUNNING || shownGame.pending.passPlayer != null ||
                    shownGame.players.any { it.lifeRequest?.state == LifeRequestState.PENDING }),
        )
        val labels = summary?.players?.associate { it.playerNumber to it.label }.orEmpty()
        val labelFor: (Int) -> String = { number -> labels[number] ?: "Player $number" }
        Column(Modifier.fillMaxSize().padding(innerPadding)) {
            if (!setup.visible && tab == HomeTab.GAME && summary != null) {
                GameSourceSwitch(source, otherUnderway = if (atlasGame) deviceUnderway else summary.state.underway(),
                    shownInLobby = (if (atlasGame) summary.state else localState.game.state) in listOf(TableState.LOBBY, TableState.GAME_OVER),
                    seatedAtAtlas = me != null) { next ->
                    chosenSource = next
                    if (next == GameSource.ATLAS) onAtlasOpened()
                }
            }
            if (!setup.visible && tab == HomeTab.PLAYERS && summary != null) {
                Row(Modifier.fillMaxWidth().padding(horizontal = 16.dp), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    listOf("Device", "Table", "Me").forEach { page ->
                        androidx.compose.material3.FilterChip(playersPage == page, { playersPage = page; if (page != "Device") onAtlasOpened() }, label = { Text(page) })
                    }
                }
            }
            // Tabs push sideways in the bar's direction, like slides; none when motion is reduced.
            AnimatedContent(
                targetState = tab,
                modifier = Modifier.weight(1f).fillMaxWidth(),
                contentAlignment = Alignment.TopCenter,
                transitionSpec = {
                    if (reduceMotion) {
                        EnterTransition.None togetherWith ExitTransition.None
                    } else {
                        val forward = targetState.ordinal > initialState.ordinal
                        val spec = tween<androidx.compose.ui.unit.IntOffset>(DesignTokens.Motion.BASE_MS)
                        (slideInHorizontally(spec) { if (forward) it / 5 else -it / 5 } + fadeIn(tween(DesignTokens.Motion.BASE_MS))) togetherWith
                            (slideOutHorizontally(spec) { if (forward) -it / 5 else it / 5 } + fadeOut(tween(DesignTokens.Motion.FAST_MS)))
                    }
                },
                label = "tab",
            ) { shown ->
                pageState.SaveableStateProvider(if (shown == HomeTab.MORE) "MORE:${morePage.name}" else shown.name) {
                    when {
                        !setup.visible && shown == HomeTab.TABLE -> localTableContent({ tab = HomeTab.GAME }, { tab = HomeTab.PLANES })
                        !setup.visible && shown == HomeTab.HISTORY -> localHistoryContent()
                        !setup.visible && shown == HomeTab.PLANES -> localPlanesContent()
                        !setup.visible && shown == HomeTab.ELDER -> localElderContent()
                        !setup.visible && shown == HomeTab.PLAYERS && (summary == null || playersPage == "Device") -> localPlayersContent()
                        !setup.visible && shown == HomeTab.GAME && !atlasGame -> localGameContent({ tab = HomeTab.TABLE }, { tab = HomeTab.PLANES }) {
                            val labelFor: (Int) -> String = { n -> deviceSummary.players.firstOrNull { it.playerNumber == n }?.label ?: "Player $n" }
                            Column(Modifier.widthIn(max = 720.dp).fillMaxWidth().verticalScroll(rememberScrollState()).padding(16.dp)) {
                                GameTab(uiState, deviceSummary, nowMs, reduceMotion, deviceGameActions, labelFor, GameSource.DEVICE)
                            }
                        }
                        else -> Column(
                            Modifier.widthIn(max = 720.dp).fillMaxWidth()
                                .verticalScroll(rememberScrollState()).padding(16.dp),
                            verticalArrangement = Arrangement.spacedBy(14.dp),
                        ) {
                            val installing = setup.visible && setup.updates is com.turnhub.android.data.UpdatesState.Installing
                            if (uiState.isOffline && summary != null && !installing) {
                                com.turnhub.android.ui.components.OfflineBanner(summary.receivedAtMs, Modifier.fillMaxWidth())
                            } else uiState.errorMessage?.takeUnless { installing }?.let {
                                ErrorCard(it, uiState.errorDetail, uiState.isRetrying, onOpenAppSettings.takeIf { uiState.offerAppSettings })
                            }
                            if (setup.visible) {
                                SetupScreen(setup, setupActions)
                            } else when (shown) {
                                HomeTab.GAME -> if (summary != null) {
                                    GameSelector(summary, onFollowGame, enabled = !uiState.isOffline && uiState.player?.busy != true && seatClaim?.waiting != true)
                                    if (updatesAvailable > 0) UpdateAvailableCard(updatesAvailable, onOpenUpdates)
                                    GameTab(uiState, summary, nowMs, reduceMotion, gameActions.copy(onOpenTablet = { showTablet = true }), labelFor, GameSource.ATLAS)
                                }
                                HomeTab.PLAYERS -> if (summary != null) {
                                    if (playersPage == "Me") {
                                        if (admin.presence?.setup == true) AdminSetupCard(adminActions)
                                        AccountTab(uiState, reduceMotion, accountActions)
                                    } else PlayersTab(summary, me?.playerNumber, nowMs, labelFor, uiState.endpointText, seatClaim, seatActions, admin.devices,
                                        uiState.player?.signedIn == true && !uiState.isOffline) {
                                        if (canPeople) PeopleCard(info, admin, uiState.avatars, adminActions)
                                    }
                                }
                                HomeTab.MORE -> MoreTab(morePage, { morePage = it }, atlas = {
                                    com.turnhub.android.ui.components.DisclosureCard("Atlas connection") {
                                        com.turnhub.android.ui.tablet.SetupSwitch("Discover Atlas", null, atlasEnabled, true, onAtlasEnabled)
                                        if (atlasEnabled) ConnectCard(uiState, onEndpointChange, onConnectClick, onDisconnectClick, discoveryActions, reduceMotion)
                                    }
                                    if (canSettings && info != null) com.turnhub.android.ui.components.DisclosureCard("Table administration") {
                                        SettingsTab(info, admin, adminActions, onShowPeople = { playersPage = "Table"; tab = HomeTab.PLAYERS })
                                    }
                                    if (canDev) com.turnhub.android.ui.components.DisclosureCard("Developer") { DevTab(admin, adminActions) }
                                }) {
                                    AppearanceCard(theme, reduceMotion, accountActions)
                                    BrassCard {
                                        val (artOn, setArt) = com.turnhub.android.ui.tablet.rememberCommanderArtEnabled()
                                        com.turnhub.android.ui.tablet.SetupSwitch("Commander card art",
                                            "When a player names their commander, its art fills their tile. The art comes from " +
                                                "Scryfall; only the commander's name is sent.", artOn, true, setArt)
                                    }
                                    BrassCard { com.turnhub.android.ui.components.GameSoundControls() }
                                    BrassCard {
                                        val (qrOnly, setQrOnly) = com.turnhub.android.ui.tablet.rememberLanQrOnly()
                                        com.turnhub.android.ui.tablet.LanSecuritySwitch(qrOnly, !sharedGameHosting, setQrOnly)
                                    }
                                    com.turnhub.android.ui.components.AppVersionLabel(Modifier.fillMaxWidth())
                                }
                                HomeTab.HISTORY, HomeTab.TABLE, HomeTab.PLANES, HomeTab.ELDER -> Unit
                            }
                        }
                    }
                }
            }
        }
    }
}

@Composable
private fun BrandBar(uiState: HomeUiState, running: Boolean, reduceMotion: Boolean,
    report: com.turnhub.android.ui.components.BugReportContext, onManualClick: () -> Unit) {
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
            Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                Text("TurnHub", color = p.text, style = MaterialTheme.typography.headlineSmall.copy(fontSize = 22.sp),
                    modifier = Modifier.semantics { heading() })
                // Atlas's battery, while connected to one that has a cell.
                val battery = uiState.tableSummary?.battery
                    ?.takeIf { uiState.connectionState == AtlasConnectionState.CONNECTED && !uiState.isOffline }
                if (battery != null) AtlasBatteryGauge(battery)
            }
            if (p.ornament) {
                Text("YOUR TABLE, YOUR TURN", color = p.muted, style = MaterialTheme.typography.labelSmall.copy(letterSpacing = 2.sp))
            }
        }
        ConnectionPill(uiState)
        Row {
            BugReportButton(report)
            ManualButton(onManualClick)
        }
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

/**
 * A small round banner button with no visible label: TalkBack reads [label],
 * and a long press shows it, so the icon is never the only explanation.
 */
@OptIn(androidx.compose.foundation.ExperimentalFoundationApi::class)
@Composable
private fun BannerButton(label: String, onClick: () -> Unit, icon: @Composable () -> Unit) {
    val p = palette
    val context = androidx.compose.ui.platform.LocalContext.current
    Box(
        Modifier
            .size(44.dp)
            .clip(CircleShape)
            .combinedClickable(role = Role.Button, onClickLabel = label, onClick = onClick, onLongClick = {
                android.widget.Toast.makeText(context, label, android.widget.Toast.LENGTH_SHORT).show()
            })
            .semantics { contentDescription = label },
        contentAlignment = Alignment.Center,
    ) {
        Box(
            Modifier
                .size(32.dp)
                .clip(CircleShape)
                .background(p.inset)
                .border(1.dp, p.line, CircleShape),
            contentAlignment = Alignment.Center,
        ) { icon() }
    }
}

/** "?" opens the user manual. */
@Composable
private fun ManualButton(onClick: () -> Unit) {
    val p = palette
    BannerButton("User manual", onClick) {
        Text("?", color = if (p.dark) p.accentHi else p.accent,
            style = MaterialTheme.typography.titleMedium.copy(fontWeight = FontWeight.Bold),
            modifier = Modifier.clearAndSetSemantics { })
    }
}

/** The bug icon explains itself, then opens the alpha bug form with editable diagnostics. */
@Composable
private fun BugReportButton(report: com.turnhub.android.ui.components.BugReportContext) {
    val p = palette
    var asking by rememberSaveable { mutableStateOf(false) }
    BannerButton("Report a bug", { asking = true }) {
        Icon(painterResource(DesignTokens.Icons.bug), contentDescription = null, modifier = Modifier.size(18.dp),
            tint = if (p.dark) p.accentHi else p.accent)
    }
    if (asking) com.turnhub.android.ui.components.BugReportDialog(onDismiss = { asking = false }, report = report)
}

@Composable
private fun ConnectionPill(uiState: HomeUiState) {
    val p = palette
    val (text, color) = when {
        uiState.joiningSsid != null -> "Joining Wi-Fi" to p.warn
        uiState.isOffline -> "Reconnecting" to p.warn
        uiState.isRetrying -> "Retrying" to p.warn
        uiState.connectionState == AtlasConnectionState.CONNECTED -> "Connected" to p.good
        uiState.connectionState == AtlasConnectionState.CONNECTING -> "Connecting" to p.warn
        else -> "On device" to p.muted
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

/** Explicit Atlas entry; automatic discovery leaves the local game on screen. */
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
        uiState.connectionState == AtlasConnectionState.CONNECTED -> "Atlas connected" to "Sigils, shared profiles and table controls are ready."
        !uiState.hasSavedTable -> "Connect to Atlas" to
            "Add Sigils, shared profiles and connected play."
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
                AccentButton("Try again", discovery.onSearchAgain, Modifier.fillMaxWidth().heightIn(min = 56.dp))
                ToneButton("Set up a new table", discovery.onSetUpNewTable, Modifier.fillMaxWidth())
            }
            !uiState.hasSavedTable -> {
                AccentButton("Set up a new table", discovery.onSetUpNewTable, Modifier.fillMaxWidth().heightIn(min = 56.dp))
                ToneButton("Connect to a table", onConnectClick, Modifier.fillMaxWidth())
            }
            else -> AccentButton("Connect to Atlas", onConnectClick, Modifier.fillMaxWidth().heightIn(min = 56.dp))
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

/**
 * One line over the Game tab while an Atlas is in reach: says where the game
 * runs and offers the other only when it makes sense (setting up or done
 * here, or a game already under way there), so a game isn't left by accident.
 */
@Composable
private fun GameSourceSwitch(source: GameSource, otherUnderway: Boolean, shownInLobby: Boolean, seatedAtAtlas: Boolean,
    onChoose: (GameSource) -> Unit) {
    val p = palette
    val atlas = source == GameSource.ATLAS
    val offer = otherUnderway
    Row(Modifier.fillMaxWidth().padding(horizontal = 16.dp), verticalAlignment = Alignment.CenterVertically) {
        Text(if (atlas) "Connected table game" else "Game on this device", color = p.muted,
            style = MaterialTheme.typography.bodyMedium, modifier = Modifier.weight(1f))
        if (offer) TextButton(onClick = { onChoose(if (atlas) GameSource.DEVICE else GameSource.ATLAS) }) {
            Text(when {
                atlas && otherUnderway -> "Back to this device's game"
                atlas -> "Play on this device"
                otherUnderway -> "Back to the Atlas game"
                else -> "Play at Atlas"
            })
        }
    }
}
