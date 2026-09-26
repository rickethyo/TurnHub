package com.turnhub.android.ui.home

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.border
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
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.semantics.LiveRegionMode
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

private enum class HomeTab(val label: String) { GAME("Game"), PLAYERS("Players"), ACCOUNT("My Account") }

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
    gameActions: GameActions = GameActions(),
    accountActions: AccountActions = AccountActions(),
    theme: TurnHubThemeChoice = TurnHubThemeChoice.BRASS,
    reduceMotion: Boolean = false,
    onSignInSubmit: (ProfileSummary, String) -> Unit = { _, _ -> },
    onSignInDismiss: () -> Unit = {},
    onAccessibilitySave: (sigilSound: Boolean, ledStyle: LedStyle, longPressMs: Int, winHoldMs: Int) -> Unit =
        { _, _, _, _ -> },
    onAccessibilityDismiss: () -> Unit = {},
) {
    uiState.wifiPrompt?.let { prompt ->
        WifiPasswordDialog(prompt = prompt, onSubmit = onWifiPasswordSubmit, onUseCurrentWifi = onUseCurrentWifi, onDismiss = onWifiPromptDismiss)
    }
    uiState.signIn?.let { prompt -> SignInDialog(prompt = prompt, onSubmit = onSignInSubmit, onDismiss = onSignInDismiss) }
    uiState.accessibility?.let { prompt ->
        AccessibilityDialog(prompt = prompt, onSave = onAccessibilitySave, onDismiss = onAccessibilityDismiss)
    }
    val p = palette
    val summary = uiState.tableSummary
    var tab by rememberSaveable { mutableStateOf(HomeTab.GAME) }
    val me = uiState.me()
    val incomingRequest = me?.lifeRequest?.let { it.state == LifeRequestState.PENDING && it.target == me.playerNumber } == true

    Scaffold(
        modifier = modifier.tableBackground(p),
        containerColor = Color.Transparent,
        topBar = { BrandBar(uiState, running = summary?.state == TableState.RUNNING, reduceMotion = reduceMotion) },
        bottomBar = {
            if (summary != null) {
                NavigationBar(containerColor = p.surface, tonalElevation = 0.dp) {
                    HomeTab.entries.forEach { entry ->
                        NavigationBarItem(
                            selected = tab == entry,
                            onClick = { tab = entry },
                            icon = {
                                BadgedBox(badge = { if (entry == HomeTab.GAME && incomingRequest) Badge { Text("!") } }) {
                                    TabIcon(entry, if (tab == entry) p.accentHi else p.muted)
                                }
                            },
                            label = { Text(entry.label) },
                            colors = NavigationBarItemDefaults.colors(
                                selectedTextColor = if (p.dark) p.accentHi else p.accent,
                                unselectedTextColor = p.muted,
                                indicatorColor = p.surface3,
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
                uiState.errorMessage?.let {
                    ErrorCard(it, uiState.errorDetail, uiState.isRetrying, onOpenAppSettings.takeIf { uiState.offerAppSettings })
                }
                if (summary == null) {
                    ConnectCard(uiState, onEndpointChange, onConnectClick, onDisconnectClick, reduceMotion)
                } else {
                    when (tab) {
                        HomeTab.GAME -> GameTab(uiState, summary, nowMs, reduceMotion, gameActions, labelFor)
                        HomeTab.PLAYERS -> PlayersTab(summary, me?.playerNumber, nowMs, labelFor, uiState.endpointText)
                        HomeTab.ACCOUNT -> AccountTab(uiState, theme, reduceMotion, accountActions.copy(onDisconnect = onDisconnectClick))
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
private fun BrandBar(uiState: HomeUiState, running: Boolean, reduceMotion: Boolean) {
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
        GearMark(34.dp, spinning = running, reduceMotion = reduceMotion)
        Column(Modifier.weight(1f)) {
            Text("TurnHub", color = p.text, style = MaterialTheme.typography.headlineSmall.copy(fontSize = 22.sp),
                modifier = Modifier.semantics { heading() })
            Text("ATLAS TABLE CONSOLE", color = p.muted, style = MaterialTheme.typography.labelSmall.copy(letterSpacing = 2.sp))
        }
        ConnectionPill(uiState)
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
    Row(
        Modifier
            .clip(RoundedCornerShape(99.dp))
            .background(p.inset)
            .border(1.dp, color.copy(alpha = .5f), RoundedCornerShape(99.dp))
            .padding(horizontal = 10.dp, vertical = 6.dp)
            .semantics(mergeDescendants = true) { liveRegion = LiveRegionMode.Polite },
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(6.dp),
    ) {
        Box(Modifier.size(8.dp).clip(CircleShape).background(color))
        Text(text, color = color, style = MaterialTheme.typography.labelMedium.copy(fontWeight = FontWeight.Bold))
    }
}

@Composable
private fun TabIcon(tab: HomeTab, color: Color) {
    Canvas(Modifier.size(24.dp).clearAndSetSemantics { }) {
        val s = size.minDimension
        val stroke = Stroke(width = s * .075f)
        when (tab) {
            HomeTab.GAME -> {
                drawCircle(color, s * .33f, Offset(s * .5f, s * .55f), style = stroke)
                drawLine(color, Offset(s * .5f, s * .55f), Offset(s * .66f, s * .39f), s * .075f)
                drawLine(color, Offset(s * .4f, s * .1f), Offset(s * .6f, s * .1f), s * .075f)
            }
            HomeTab.PLAYERS -> {
                drawCircle(color, s * .14f, Offset(s * .37f, s * .33f), style = stroke)
                drawArc(color, 180f, 180f, false, Offset(s * .12f, s * .6f), Size(s * .5f, s * .5f), style = stroke)
                drawCircle(color, s * .11f, Offset(s * .72f, s * .37f), style = stroke)
                drawArc(color, 200f, 140f, false, Offset(s * .56f, s * .62f), Size(s * .36f, s * .4f), style = stroke)
            }
            HomeTab.ACCOUNT -> {
                drawCircle(color, s * .17f, Offset(s * .5f, s * .32f), style = stroke)
                drawArc(color, 180f, 180f, false, Offset(s * .18f, s * .6f), Size(s * .64f, s * .6f), style = stroke)
            }
        }
    }
}

@Composable
private fun ConnectCard(
    uiState: HomeUiState,
    onEndpointChange: (String) -> Unit,
    onConnectClick: () -> Unit,
    onDisconnectClick: () -> Unit,
    reduceMotion: Boolean,
) {
    val p = palette
    BrassCard {
        Column(Modifier.fillMaxWidth(), horizontalAlignment = Alignment.CenterHorizontally, verticalArrangement = Arrangement.spacedBy(10.dp)) {
            GearMark(96.dp, spinning = uiState.connectionState == AtlasConnectionState.CONNECTING || uiState.joiningSsid != null,
                reduceMotion = reduceMotion)
            Text("Join your table", style = MaterialTheme.typography.headlineMedium, color = p.text,
                modifier = Modifier.semantics { heading() })
            Text(
                "TurnHub joins the Atlas Wi-Fi for you, then shows the live table: turns, life, requests and your seat.",
                color = p.muted,
                textAlign = TextAlign.Center,
            )
        }
        Eyebrow("Atlas")
        OutlinedTextField(
            value = uiState.endpointText,
            onValueChange = onEndpointChange,
            enabled = uiState.endpointEditable,
            singleLine = true,
            label = { Text("Atlas address") },
            keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Uri, imeAction = ImeAction.Go),
            keyboardActions = KeyboardActions(onGo = { onConnectClick() }),
            modifier = Modifier.fillMaxWidth(),
        )
        when {
            uiState.joiningSsid != null -> AccentButton("Joining ${uiState.joiningSsid} Wi-Fi…", {}, Modifier.fillMaxWidth(), enabled = false)
            uiState.connectionState == AtlasConnectionState.CONNECTING -> AccentButton("Connecting…", {}, Modifier.fillMaxWidth(), enabled = false)
            uiState.connectionState == AtlasConnectionState.CONNECTED -> ToneButton("Disconnect", onDisconnectClick, Modifier.fillMaxWidth())
            else -> AccentButton("Connect to Atlas", onConnectClick, Modifier.fillMaxWidth().height(56.dp))
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
