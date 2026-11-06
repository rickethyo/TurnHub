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
import androidx.compose.foundation.layout.safeDrawingPadding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.statusBarsPadding
import androidx.compose.foundation.layout.width
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
import androidx.compose.material3.Snackbar
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.text.rememberTextMeasurer
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
import androidx.compose.ui.semantics.liveRegion
import androidx.compose.ui.semantics.stateDescription
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
import com.turnhub.android.ui.components.AmbientBackground
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
    PLAYERS("Players", DesignTokens.Icons.users),
    HISTORY("History", DesignTokens.Icons.clock),
    MORE("More", DesignTokens.Icons.more),
}

/**
 * One navigation shell: this device runs the game; a connected Atlas adds its
 * screen, Sigils, profiles, records and settings.
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
    atlasEnabled: Boolean = true,
    onAtlasEnabled: (Boolean) -> Unit = {},
    localState: com.turnhub.android.standalone.StandaloneState = com.turnhub.android.standalone.StandaloneState(),
    localTable: com.turnhub.android.standalone.StandaloneTable? = null,
    onOpenSharedGame: (() -> Unit)? = null,
    sharedRoomHeader: @Composable () -> Unit = {},
    sharedRoomFeedback: @Composable () -> Unit = {},
    sharedGameHosting: Boolean = false,
    sharedGameCode: String? = null,
    /** Phone-hosted games: this phone's game shown on the connected Atlas. */
    hostedStatus: com.turnhub.android.data.HostedAtlasLink.Status = com.turnhub.android.data.HostedAtlasLink.Status.Off,
    hostedPlanesMessage: String? = null,
    phonePairing: com.turnhub.android.data.AtlasPhonePairing.State = com.turnhub.android.data.AtlasPhonePairing.State.Idle,
    onConfirmPhoneFingerprint: () -> Unit = {},
    onHostOnAtlas: ((Boolean) -> Unit)? = null,
    /** Atlas kept the last hub's unfinished game: whose it was, while it is offered. */
    hostedResumeHost: String? = null,
    onResumeHosted: (Boolean) -> Unit = {},
) {
    PresenceCodeDialog(admin, adminActions)
    if (phonePairing is com.turnhub.android.data.AtlasPhonePairing.State.Asking) {
        androidx.compose.ui.window.Dialog(
            onDismissRequest = onDisconnectClick,
            properties = androidx.compose.ui.window.DialogProperties(usePlatformDefaultWidth = false),
        ) {
            androidx.compose.material3.Surface(Modifier.fillMaxSize(), color = palette.bg) {
                Column(
                    Modifier.fillMaxSize().safeDrawingPadding().verticalScroll(rememberScrollState()).padding(24.dp),
                    horizontalAlignment = Alignment.CenterHorizontally,
                    verticalArrangement = Arrangement.spacedBy(24.dp, Alignment.CenterVertically),
                ) {
                    Text("Approve this phone", style = MaterialTheme.typography.headlineMedium,
                        modifier = Modifier.semantics { heading() })
                    Text("Compare this code with the code on Atlas.", textAlign = TextAlign.Center)
                    Text(phonePairing.code, style = MaterialTheme.typography.displayMedium,
                        modifier = Modifier.semantics { liveRegion = LiveRegionMode.Polite })
                    Text(
                        if (phonePairing.requiresFingerprintConfirmation) {
                            if (phonePairing.fingerprintConfirmed) {
                                "Fingerprint confirmed. Waiting for Allow on Atlas."
                            } else {
                                "If they match, tap Allow on Atlas, then confirm the fingerprint here."
                            }
                        } else {
                            "If the codes match, tap Allow on Atlas. This screen closes when pairing finishes."
                        },
                        textAlign = TextAlign.Center)
                    if (phonePairing.requiresFingerprintConfirmation) {
                        TextButton(
                            enabled = !phonePairing.fingerprintConfirmed,
                            onClick = onConfirmPhoneFingerprint,
                        ) {
                            Text(if (phonePairing.fingerprintConfirmed) "Fingerprint confirmed" else "Codes match · Trust Atlas")
                        }
                    }
                    ToneButton("Cancel pairing", onDisconnectClick, Modifier.fillMaxWidth())
                }
            }
        }
    }
    uiState.homePairing?.let { atlas ->
        androidx.compose.material3.AlertDialog(
            onDismissRequest = discoveryActions.onDismissHomePairing,
            title = { Text("Link this Atlas?") },
            text = {
                Column(verticalArrangement = Arrangement.spacedBy(12.dp)) {
                    Text("Found ${atlas.atlasId} on your Wi-Fi. On Atlas, open Settings and find App link. Only continue if its code matches:")
                    Text(atlas.linkCode, style = MaterialTheme.typography.headlineSmall)
                }
            },
            confirmButton = { TextButton(onClick = discoveryActions.onConfirmHomePairing) { Text("Codes match · Link Atlas") } },
            dismissButton = { TextButton(onClick = discoveryActions.onDismissHomePairing) { Text("Cancel") } },
        )
    }
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
    val info = uiState.sessionInfo()
    val canSettings = info?.has(com.turnhub.android.protocol.AccountPermission.ADMIN) == true
    val canPeople = canSettings || info?.has(com.turnhub.android.protocol.AccountPermission.GAME_MASTER) == true
    val canDev = info?.has(com.turnhub.android.protocol.AccountPermission.DEVELOPER) == true
    val deviceSummary = androidx.compose.runtime.remember(localState.game, localState.library.players) {
        localState.game.toSummary(System.currentTimeMillis(), com.turnhub.android.domain.TableClock.nowMs(),
            localState.library.players.associate { it.localId to it.personalization })
    }
    // The Planes tab shows only while Planechase is on (Game setup's toggle turns it on).
    val planes = localState.game.profile.magicRules && localState.game.planechase.enabled
    val tabs = HomeTab.entries.filter { it != HomeTab.PLANES || planes }
    // Leaving Magic or turning Planechase off hides the Planes tab; whoever was on it goes back to Game.
    androidx.compose.runtime.LaunchedEffect(planes) { if (!planes && tab == HomeTab.PLANES) tab = HomeTab.GAME }
    var showPeople by rememberSaveable { mutableStateOf(false) }
    val pageState = androidx.compose.runtime.saveable.rememberSaveableStateHolder()
    androidx.activity.compose.BackHandler(enabled = tab != HomeTab.GAME && !setup.visible) {
        if (tab == HomeTab.MORE && morePage != MorePage.HOME) morePage = MorePage.HOME
        else tab = HomeTab.GAME
    }
    // Presence also says whether Atlas still needs its first Admin.
    androidx.compose.runtime.LaunchedEffect(info?.profileId) { if (info != null) adminActions.onRefresh() }
    // The bundled user manual, reachable from the top bar before or after connecting.
    var showManual by rememberSaveable { mutableStateOf(false) }
    if (showManual) {
        ManualScreen(onClose = { showManual = false }, modifier = modifier)
        return
    }
    // A saved/background Atlas connection must not label a device game as Atlas play.
    val reportFromAtlas = summary != null && (setup.visible || tab == HomeTab.PLAYERS)
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

    val playing = deviceSummary.state == TableState.RUNNING
    AmbientBackground(reduceMotion, modifier.fillMaxSize(), quiet = !setup.visible && tab != HomeTab.MORE &&
        (playing || tab in listOf(HomeTab.TABLE, HomeTab.PLANES))) {
    Scaffold(
        modifier = Modifier.fillMaxSize(),
        containerColor = Color.Transparent,
        snackbarHost = {
            Column {
                sharedRoomFeedback()
                if (!setup.visible && hostedStatus == com.turnhub.android.data.HostedAtlasLink.Status.Hosting && hostedPlanesMessage != null)
                    Snackbar(Modifier.padding(12.dp)) { Text(hostedPlanesMessage, maxLines = 4) }
            }
        },
        topBar = {
            Column(Modifier.background(p.bg.copy(alpha = .92f))) {
                BrandBar(uiState, running = playing, reduceMotion = reduceMotion,
                    report = reportContext,
                    onManualClick = { showManual = true },
                    onGearClick = { tab = HomeTab.MORE; morePage = MorePage.ABOUT })
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
                                Icon(painterResource(entry.icon), contentDescription = null, modifier = Modifier.size(24.dp))
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
        Column(Modifier.fillMaxSize().padding(innerPadding)) {
            if (!setup.visible && tab == HomeTab.GAME && hostedResumeHost != null) {
                androidx.compose.foundation.layout.FlowRow(Modifier.fillMaxWidth().padding(horizontal = 8.dp)) {
                    TextButton(onClick = { onResumeHosted(true) }) { Text("Resume " +
                        (hostedResumeHost.takeIf { it.isNotBlank() }?.let { "$it's" } ?: "Atlas") + " game") }
                    TextButton(onClick = { onResumeHosted(false) }) { Text("Show mine instead") }
                }
            }
            // Pairing replaces signing in: Atlas asks "Allow this phone?" with the same code.
            val pairingLine = when (phonePairing) {
                is com.turnhub.android.data.AtlasPhonePairing.State.Asking ->
                    "Tap Allow on the Atlas screen to pair this phone. Code ${phonePairing.code}"
                is com.turnhub.android.data.AtlasPhonePairing.State.Failed -> phonePairing.message
                else -> null
            }
            if (!setup.visible && phonePairing is com.turnhub.android.data.AtlasPhonePairing.State.Failed && pairingLine != null) {
                Text(pairingLine, style = MaterialTheme.typography.bodyMedium,
                    modifier = Modifier.padding(horizontal = 16.dp).semantics { liveRegion = androidx.compose.ui.semantics.LiveRegionMode.Polite })
            }
            if (!setup.visible && tab in listOf(HomeTab.GAME, HomeTab.TABLE, HomeTab.PLANES)) sharedRoomHeader()
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
                pageState.SaveableStateProvider(when (shown) {
                    HomeTab.MORE -> "MORE:${morePage.name}"
                    else -> shown.name
                }) {
                    when {
                        !setup.visible && shown == HomeTab.TABLE -> localTableContent({ tab = HomeTab.GAME }, { tab = HomeTab.PLANES })
                        !setup.visible && shown == HomeTab.HISTORY -> localHistoryContent()
                        !setup.visible && shown == HomeTab.PLANES -> localPlanesContent()
                        !setup.visible && shown == HomeTab.PLAYERS -> localPlayersContent()
                        !setup.visible && shown == HomeTab.GAME -> localGameContent({ tab = HomeTab.TABLE }, { tab = HomeTab.PLANES }) {
                            Column(Modifier.widthIn(max = 720.dp).fillMaxSize().padding(4.dp)) {
                                if (localTable != null) com.turnhub.android.ui.tablet.LocalTable(localState, localTable, reduceMotion,
                                    onClose = { tab = HomeTab.GAME }, onOpenPlanes = { tab = HomeTab.PLANES }, phoneView = true)
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
                                HomeTab.GAME -> Unit
                                HomeTab.PLAYERS -> Unit
                                HomeTab.MORE -> MoreTab(morePage, { morePage = it }, atlas = {
                                    com.turnhub.android.ui.components.DisclosureCard("Private stats and administration") { AccountTab(uiState, reduceMotion, accountActions) }
                                    if (canPeople && info != null) {
                                        TextButton(onClick = { showPeople = !showPeople }) { Text(if (showPeople) "Hide people and permissions" else "People and permissions") }
                                        if (showPeople) PeopleCard(info, admin, uiState.avatars, adminActions)
                                    }
                                    com.turnhub.android.ui.components.DisclosureCard("Atlas connection") {
                                        (hostedStatus as? com.turnhub.android.data.HostedAtlasLink.Status.Failed)?.let {
                                            Text(it.message, color = p.warn)
                                        }
                                        (hostedStatus as? com.turnhub.android.data.HostedAtlasLink.Status.Busy)?.let {
                                            Text("Atlas is showing " + (it.host ?: "another phone") + "'s game.", color = p.muted)
                                        }
                                        if (onHostOnAtlas != null && summary != null) TextButton(onClick = {
                                            onHostOnAtlas(hostedStatus != com.turnhub.android.data.HostedAtlasLink.Status.Hosting)
                                        }) { Text(if (hostedStatus == com.turnhub.android.data.HostedAtlasLink.Status.Hosting) "Stop showing on Atlas" else "Show on Atlas") }
                                        com.turnhub.android.ui.tablet.SetupSwitch("Discover Atlas", null, atlasEnabled, true, onAtlasEnabled)
                                        if (atlasEnabled) ConnectCard(uiState, onEndpointChange, onConnectClick, onDisconnectClick, discoveryActions, reduceMotion)
                                    }
                                    // Atlas settings (the admin portal's, since 2026-10-30): always listed while connected.
                                    if (summary != null) com.turnhub.android.ui.components.DisclosureCard("Atlas settings") {
                                        if (canSettings && info != null) SettingsTab(info, admin, adminActions, onShowPeople = { showPeople = true })
                                        else {
                                            Text(if (info == null) "Unlock Admin access with the profile PIN to change Wi-Fi, Sigils, volume and accounts."
                                                else "Unlock an Admin profile with its PIN to change these settings.",
                                                color = com.turnhub.android.ui.theme.palette.muted, style = MaterialTheme.typography.bodyMedium)
                                            com.turnhub.android.ui.components.ToneButton("Unlock Admin access", { accountActions.onPlayFromPhone() }, Modifier.fillMaxWidth())
                                        }
                                    }
                                    if (canDev) com.turnhub.android.ui.components.DisclosureCard("Developer") { DevTab(admin, adminActions) }
                                }) {
                                    AppearanceCard(theme, reduceMotion, accountActions)
                                    BrassCard {
                                        val owner = localState.library.deviceAdmin
                                        com.turnhub.android.ui.tablet.SetupSwitch("Keep profiles copied from Atlas",
                                            "Keep copied profiles on this device after disconnecting. If off, unused automatic copies are removed after 24 hours offline; game records stay.",
                                            localState.library.keepCopiedProfiles,
                                            localTable != null && (owner == null || localTable.accountUnlocked(owner)),
                                            { localTable?.keepCopiedProfiles(it) })
                                        if (owner != null && localTable?.accountUnlocked(owner) != true)
                                            Text("Unlock this device's Admin profile to change this setting.", color = palette.muted)
                                    }
                                    BrassCard {
                                        val (artOn, setArt) = com.turnhub.android.ui.tablet.rememberCommanderArtEnabled()
                                        com.turnhub.android.ui.tablet.SetupSwitch("Commander card art",
                                            "When a player names their commander, its art fills their tile. The art comes from " +
                                                "Scryfall; only the commander's name is sent.", artOn, true, setArt)
                                    }
                                    com.turnhub.android.ui.components.DisclosureCard("Sounds") {
                                        com.turnhub.android.ui.components.GameSoundControls()
                                        if (canDev) TextButton(onClick = { adminActions.run { testAtlasSpeaker() } },
                                            enabled = !admin.busy && !uiState.isOffline) {
                                            Text("Test Atlas speaker")
                                        }
                                    }
                                    BrassCard {
                                        val (qrOnly, setQrOnly) = com.turnhub.android.ui.tablet.rememberLanQrOnly()
                                        com.turnhub.android.ui.tablet.LanSecuritySwitch(qrOnly, !sharedGameHosting, setQrOnly)
                                    }
                                    com.turnhub.android.ui.components.AppVersionLabel(Modifier.fillMaxWidth())
                                }
                                HomeTab.HISTORY, HomeTab.TABLE, HomeTab.PLANES -> Unit
                            }
                        }
                    }
                }
            }
        }
    }
    }
}

@OptIn(androidx.compose.foundation.layout.ExperimentalLayoutApi::class)
@Composable
internal fun BrandBar(uiState: HomeUiState, running: Boolean, reduceMotion: Boolean,
    report: com.turnhub.android.ui.components.BugReportContext, onManualClick: () -> Unit, onGearClick: () -> Unit) {
    val p = palette
    Column(
        Modifier
            .fillMaxWidth()
            .background(p.bg.copy(alpha = .92f))
            .statusBarsPadding()
            .padding(horizontal = 16.dp, vertical = 10.dp),
        verticalArrangement = Arrangement.spacedBy(4.dp),
    ) {
        Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(10.dp)) {
        // The gear opens About TurnHub; no ripple, so it looks the same as before.
        androidx.compose.foundation.layout.Box(Modifier.size(44.dp).clickable(
            interactionSource = androidx.compose.runtime.remember { androidx.compose.foundation.interaction.MutableInteractionSource() },
            indication = null, role = Role.Button, onClickLabel = "Open About TurnHub", onClick = onGearClick,
        ).semantics { contentDescription = "About TurnHub" }, contentAlignment = Alignment.Center) {
            GearMark(if (p.ornament) 34.dp else 28.dp, spinning = running, reduceMotion = reduceMotion)
        }
        Text("TurnHub", color = p.text, style = MaterialTheme.typography.headlineSmall.copy(fontSize = 22.sp),
            modifier = Modifier.weight(1f).semantics { heading() }, maxLines = 1)
        BugReportButton(report)
        ManualButton(onManualClick)
        }
        androidx.compose.foundation.layout.FlowRow(Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(12.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
            ConnectionPill(uiState)
            if (uiState.connectionState == AtlasConnectionState.CONNECTED) {
                uiState.tableSummary?.battery?.let { AtlasBatteryGauge(it) }
                uiState.tableSummary?.links?.let { com.turnhub.android.ui.components.AtlasLinkIcons(it) }
            }
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
internal fun ConnectionPill(uiState: HomeUiState) {
    val p = palette
    val (text, color) = when {
        uiState.joiningSsid != null -> "Joining Wi-Fi" to p.warn
        uiState.isOffline -> "Reconnecting" to p.warn
        uiState.isRetrying -> "Retrying" to p.warn
        uiState.connectionState == AtlasConnectionState.CONNECTED -> "Connected" to p.good
        uiState.connectionState == AtlasConnectionState.CONNECTING -> "Connecting" to p.warn
        else -> "On device" to p.muted
    }
    val style = MaterialTheme.typography.labelMedium.copy(fontWeight = FontWeight.Bold)
    val measurer = rememberTextMeasurer()
    val labelWidth = with(LocalDensity.current) {
        listOf("Joining Wi-Fi", "Reconnecting", "Retrying", "Connected", "Connecting", "On device")
            .maxOf { measurer.measure(it, style, maxLines = 1).size.width }.toDp()
    }
    val pill = RoundedCornerShape(99.dp)
    Row(
        Modifier.width(labelWidth + 36.dp)
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
        Text(text, color = color, style = style, maxLines = 1, softWrap = false)
    }
}

/** The connect screen's retry and new-table actions. */
data class DiscoveryActions(
    val onSearchAgain: () -> Unit = {},
    val onSetUpNewTable: () -> Unit = {},
    val onConfirmHomePairing: () -> Unit = {},
    val onDismissHomePairing: () -> Unit = {},
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
        uiState.discovery == Discovery.Searching -> "Looking for your table" to "Looking for Atlas on your Wi-Fi…"
        uiState.discovery == Discovery.NotFound -> "Couldn't reach your table" to
            "Check that Atlas is switched on and close by. If it was factory reset, set it up again."
        uiState.connectionState == AtlasConnectionState.CONNECTED -> "Atlas connected" to "Sigils, shared profiles and table controls are ready."
        !uiState.hasSavedTable -> "Connect to Atlas" to
            "Add Sigils, shared profiles and connected play."
        else -> "Join your table" to "Find Atlas on your home Wi-Fi, or set up a new Atlas using its on-screen QR code."
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
                ToneButton("Set up an Atlas", discovery.onSetUpNewTable, Modifier.fillMaxWidth())
            }
            !uiState.hasSavedTable -> {
                AccentButton("Find Atlas on Wi-Fi", onConnectClick, Modifier.fillMaxWidth().heightIn(min = 56.dp))
                ToneButton("Set up an Atlas", discovery.onSetUpNewTable, Modifier.fillMaxWidth())
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
                supportingText = { Text("Leave it at https://192.168.4.1 unless Atlas joined another network.") },
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
