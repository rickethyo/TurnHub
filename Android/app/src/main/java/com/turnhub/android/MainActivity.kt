package com.turnhub.android

import android.Manifest
import android.content.Intent
import android.content.pm.ApplicationInfo
import android.content.pm.PackageManager
import android.net.Uri
import android.os.Build
import android.hardware.biometrics.BiometricManager
import android.hardware.biometrics.BiometricPrompt
import android.os.Bundle
import android.os.CancellationSignal
import android.provider.Settings
import androidx.activity.ComponentActivity
import androidx.activity.SystemBarStyle
import androidx.activity.enableEdgeToEdge
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.activity.viewModels
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.lifecycle.lifecycleScope
import androidx.lifecycle.repeatOnLifecycle
import com.turnhub.android.data.KeystoreProfileVault
import com.turnhub.android.data.TurnNotifier
import com.turnhub.android.ui.home.AppLockRequest
import com.turnhub.android.data.UpdateNotifier
import kotlinx.coroutines.launch
import com.turnhub.android.data.AtlasLinkHoldService
import com.turnhub.android.data.AtlasPlayerSession
import com.turnhub.android.data.AtlasUpdateWatcher
import com.turnhub.android.protocol.AtlasConnectionState
import com.turnhub.android.ui.home.AccountActions
import com.turnhub.android.ui.home.GameActions
import com.turnhub.android.ui.theme.TurnHubThemeChoice
import com.turnhub.android.data.AtlasSessionTransportFactory
import com.turnhub.android.data.AtlasTransportFactory
import com.turnhub.android.data.HttpAtlasRepository
import com.turnhub.android.data.HttpAtlasTransport
import com.turnhub.android.data.PreferencesWifiCredentialStore
import com.turnhub.android.data.TargetedAtlasWifiLink
import com.turnhub.android.ui.home.HomeScreen
import com.turnhub.android.ui.home.HomeViewModel
import com.turnhub.android.ui.theme.TurnHubTheme
import com.turnhub.android.ui.theme.palette

/**
 * Single-Activity host for this milestone's one screen. A later milestone may
 * introduce navigation between lobby/game/settings screens; see Android/README.md.
 */
class MainActivity : ComponentActivity() {

    // Manual, minimal composition root: the live HTTP repository, owned by the
    // ViewModel so polling survives rotation. The endpoint is chosen by the
    // user on the Home screen and passed in at connect time.
    private val homeViewModel: HomeViewModel by viewModels {
        // One link both joins the Atlas Wi-Fi and routes Atlas requests over it
        // (falling back to a manually joined Wi-Fi when it holds no network).
        val wifiLink = TargetedAtlasWifiLink(applicationContext)
        val transports = AtlasTransportFactory { endpoint -> HttpAtlasTransport(endpoint, wifiLink) }
        val sessionTransports = AtlasSessionTransportFactory { endpoint -> HttpAtlasTransport(endpoint, wifiLink) }
        val playerSession = AtlasPlayerSession(sessionTransports)
        HomeViewModel.factory(
            repositoryFactory = { scope -> HttpAtlasRepository(transports, scope) },
            wifiLink = wifiLink,
            credentialStore = PreferencesWifiCredentialStore(applicationContext),
            playerSession = playerSession,
            // Development builds look for new firmware every minute, release builds daily.
            updateCheckIntervalMs = if (applicationInfo.flags and ApplicationInfo.FLAG_DEBUGGABLE != 0) {
                AtlasUpdateWatcher.DEBUG_INTERVAL_MS
            } else {
                AtlasUpdateWatcher.RELEASE_INTERVAL_MS
            },
            vault = KeystoreProfileVault(applicationContext).takeIf { it.available },
        )
    }

    // On launch: local network permission (Android 17), then rejoin the saved table.
    private val launchPermission =
        registerForActivityResult(ActivityResultContracts.RequestPermission()) { granted ->
            if (granted) homeViewModel.onAppStarted() else homeViewModel.onLocalNetworkPermissionDenied()
        }

    private fun findTableOnLaunch() {
        if (Build.VERSION.SDK_INT >= LOCAL_NETWORK_PERMISSION_SDK &&
            checkSelfPermission(Manifest.permission.ACCESS_LOCAL_NETWORK) != PackageManager.PERMISSION_GRANTED
        ) {
            launchPermission.launch(Manifest.permission.ACCESS_LOCAL_NETWORK)
        } else {
            homeViewModel.onAppStarted()
        }
    }

    // Android 17 blocks local-network traffic (so every Atlas request would just
    // time out) until the user grants ACCESS_LOCAL_NETWORK ("Nearby devices").
    // Asked on launch, and again here on Connect if it was refused then.
    private val localNetworkPermission =
        registerForActivityResult(ActivityResultContracts.RequestPermission()) { granted ->
            if (granted) homeViewModel.onConnectClicked() else homeViewModel.onLocalNetworkPermissionDenied()
        }

    private fun connectToAtlas() {
        if (Build.VERSION.SDK_INT >= LOCAL_NETWORK_PERMISSION_SDK &&
            checkSelfPermission(Manifest.permission.ACCESS_LOCAL_NETWORK) != PackageManager.PERMISSION_GRANTED
        ) {
            localNetworkPermission.launch(Manifest.permission.ACCESS_LOCAL_NETWORK)
        } else {
            homeViewModel.onConnectClicked()
        }
    }

    // The notification-bar "Update available" (Android 13+ asks first, once per
    // run, when the first update appears; refusing leaves the in-app card).
    private val updateNotifier by lazy { UpdateNotifier(applicationContext) }
    private var notificationAsked = false
    private var lastNotified = 0
    private val notificationPermission =
        registerForActivityResult(ActivityResultContracts.RequestPermission()) { granted ->
            if (granted && lastNotified > 0 && !lifecycle.currentState.isAtLeast(Lifecycle.State.STARTED)) {
                updateNotifier.show(lastNotified)
            }
        }

    private fun onUpdatesAvailable(count: Int) {
        if (count == 0) {
            lastNotified = 0
            updateNotifier.clear()
            return
        }
        val foreground = lifecycle.currentState.isAtLeast(Lifecycle.State.STARTED)
        if (!UpdateNotifier.shouldNotify(lastNotified, count)) return
        if (foreground) {
            // The card on the table screen is the notice; ask for the bar one now.
            if (!updateNotifier.allowed() && !notificationAsked && Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                notificationAsked = true
                notificationPermission.launch(Manifest.permission.POST_NOTIFICATIONS)
            }
            lastNotified = count
            return
        }
        lastNotified = count
        updateNotifier.show(count)
    }

    /**
     * App lock: the phone's own fingerprint, face or screen-lock check, run
     * whenever the ViewModel asks to save or use the saved profile. The vault's
     * key opens for ProfileVault.AUTH_SECONDS after it passes. Android 11+.
     */
    private var appLockShowing: AppLockRequest? = null

    private fun runAppLock(request: AppLockRequest?) {
        if (request == null || request === appLockShowing) return
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.R) {
            homeViewModel.onAppLockResult(false)
            return
        }
        appLockShowing = request
        val prompt = BiometricPrompt.Builder(this)
            .setTitle(if (request is AppLockRequest.Save) "Sign in automatically?" else "Sign in to TurnHub")
            .setSubtitle(
                if (request is AppLockRequest.Save) {
                    "Confirm it's you to keep ${request.profile.name} signed in on this phone"
                } else {
                    "Signing in as ${request.profile.name}"
                },
            )
            .setAllowedAuthenticators(
                BiometricManager.Authenticators.BIOMETRIC_STRONG or BiometricManager.Authenticators.DEVICE_CREDENTIAL,
            )
            .build()
        prompt.authenticate(
            CancellationSignal(),
            mainExecutor,
            object : BiometricPrompt.AuthenticationCallback() {
                override fun onAuthenticationSucceeded(result: BiometricPrompt.AuthenticationResult) {
                    appLockShowing = null
                    homeViewModel.onAppLockResult(true)
                }

                // Cancelled, too many tries, or no screen lock set: sign in by hand instead.
                override fun onAuthenticationError(errorCode: Int, errString: CharSequence) {
                    appLockShowing = null
                    homeViewModel.onAppLockResult(false)
                }
            },
        )
    }

    /** After a permanent denial Android shows no dialog; the user must allow it here. */
    private fun openAppSettings() {
        startActivity(
            Intent(Settings.ACTION_APPLICATION_DETAILS_SETTINGS, Uri.fromParts("package", packageName, null)),
        )
    }

    // Appearance is a phone-only preference, like the portal's per-browser theme.
    private val uiPrefs by lazy { getSharedPreferences("turnhub_ui", MODE_PRIVATE) }
    private var theme by mutableStateOf(TurnHubThemeChoice.AUTO)
    private var reduceMotion by mutableStateOf(false)

    private fun chooseTheme(choice: TurnHubThemeChoice) {
        theme = choice
        uiPrefs.edit().putString("theme", choice.key).apply()
    }

    private fun chooseReduceMotion(on: Boolean) {
        reduceMotion = on
        uiPrefs.edit().putBoolean("reduceMotion", on).apply()
    }

    /** Hands Atlas's serial log to another app (mail, Drive, a chat) as text. */
    private fun shareLog(text: String) {
        val send = Intent(Intent.ACTION_SEND).apply {
            type = "text/plain"
            putExtra(Intent.EXTRA_SUBJECT, "TurnHub Atlas serial log")
            // Binder transactions are limited; keep the newest part of a long log.
            putExtra(Intent.EXTRA_TEXT, if (text.length > 200_000) text.takeLast(200_000) else text)
        }
        startActivity(Intent.createChooser(send, "Share serial log"))
    }

    /** Hands the signed-in profile's statistics report to another app as text, like the portal's download. */
    private fun shareStats(text: String) {
        val send = Intent(Intent.ACTION_SEND).apply {
            type = "text/plain"
            putExtra(Intent.EXTRA_SUBJECT, "My TurnHub statistics")
            putExtra(Intent.EXTRA_TEXT, text)
        }
        startActivity(Intent.createChooser(send, "Share my stats"))
    }

    private val turnNotifier by lazy { TurnNotifier(applicationContext) }

    override fun onStart() {
        super.onStart()
        turnNotifier.clear()
    }

    override fun onStop() {
        super.onStop()
        if (!isChangingConfigurations) turnNotifier.show(homeViewModel.liveTurn.value)
    }

    override fun onResume() {
        super.onResume()
        AtlasLinkHoldService.release(this)
    }

    // Leaving the screen while connected: hold the Atlas Wi-Fi for a quick
    // app switch (AtlasLinkHoldService). Started here, while the app is still
    // foreground, because Android refuses foreground services started later.
    override fun onPause() {
        super.onPause()
        if (!isChangingConfigurations &&
            homeViewModel.uiState.value.connectionState == AtlasConnectionState.CONNECTED
        ) {
            AtlasLinkHoldService.hold(this)
        }
    }

    override fun onDestroy() {
        if (!isChangingConfigurations) {
            AtlasLinkHoldService.release(this)
            turnNotifier.clear()
        }
        super.onDestroy()
    }

    /** Transparent system bars whose icons suit the theme drawn behind them. */
    private fun matchSystemBars(dark: Boolean) {
        val style = if (dark) {
            SystemBarStyle.dark(android.graphics.Color.TRANSPARENT)
        } else {
            SystemBarStyle.light(android.graphics.Color.TRANSPARENT, android.graphics.Color.TRANSPARENT)
        }
        enableEdgeToEdge(statusBarStyle = style, navigationBarStyle = style)
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        // Edge to edge from the first frame; the theme corrects the bar icons below.
        enableEdgeToEdge()
        super.onCreate(savedInstanceState)
        theme = TurnHubThemeChoice.fromKey(uiPrefs.getString("theme", null))
        reduceMotion = uiPrefs.getBoolean("reduceMotion", false)
        // Not again on rotation (the ViewModel also runs it once).
        if (savedInstanceState == null) findTableOnLaunch()
        lifecycleScope.launch { homeViewModel.updatesAvailable.collect(::onUpdatesAvailable) }
        lifecycleScope.launch {
            repeatOnLifecycle(Lifecycle.State.RESUMED) { homeViewModel.appLock.collect(::runAppLock) }
        }
        // The lock-screen turn clock: only while the app is out of sight (the
        // screen itself shows the turn otherwise). Runs until the Activity ends.
        lifecycleScope.launch {
            homeViewModel.liveTurn.collect { turn ->
                if (!lifecycle.currentState.isAtLeast(Lifecycle.State.STARTED)) turnNotifier.show(turn)
            }
        }
        setContent {
            TurnHubTheme(choice = theme) {
                val dark = palette.dark
                LaunchedEffect(dark) { matchSystemBars(dark) }
                Surface(
                    modifier = Modifier.fillMaxSize(),
                    color = MaterialTheme.colorScheme.background,
                ) {
                    val uiState by homeViewModel.uiState.collectAsStateWithLifecycle()
                    HomeScreen(
                        uiState = uiState,
                        onEndpointChange = homeViewModel::onEndpointChanged,
                        onConnectClick = ::connectToAtlas,
                        onDisconnectClick = homeViewModel::onDisconnectClicked,
                        onOpenAppSettings = ::openAppSettings,
                        onWifiPasswordSubmit = homeViewModel::onWifiPasswordSubmitted,
                        onUseCurrentWifi = homeViewModel::onUseCurrentWifi,
                        onWifiPromptDismiss = homeViewModel::onWifiPromptDismissed,
                        discoveryActions = com.turnhub.android.ui.home.DiscoveryActions(
                            onSearchAgain = homeViewModel::onSearchAgain,
                            onSetUpNewTable = homeViewModel::onSetUpNewTable,
                        ),
                        gameActions = GameActions(
                            onControl = homeViewModel::onControl,
                            onJoin = homeViewModel::onJoinClicked,
                            onPlayFromPhone = homeViewModel::onPlayFromPhoneClicked,
                            onChangeMyLife = homeViewModel::onChangeMyLife,
                            onRequestLife = homeViewModel::onRequestLife,
                            onRespondLife = homeViewModel::onRespondLife,
                            onCommanderDamage = homeViewModel::onCommanderDamage,
                            onSaveGameSettings = homeViewModel::onSaveGameSettings,
                        ),
                        accountActions = AccountActions(
                            onPlayFromPhone = homeViewModel::onPlayFromPhoneClicked,
                            onSaveName = homeViewModel::onSaveName,
                            onSavePin = homeViewModel::onSavePin,
                            onLoadChoices = homeViewModel::onLoadChoices,
                            onSavePolicy = homeViewModel::onSavePolicy,
                            onClearPin = homeViewModel::onClearPin,
                            onLoadStats = homeViewModel::onLoadStats,
                            onShareStats = { homeViewModel.onExportStats(::shareStats) },
                            onLoadPersonalization = homeViewModel::onLoadPersonalization,
                            onSavePersonalization = homeViewModel::onSavePersonalization,
                            onAccessibility = homeViewModel::onAccessibilityClicked,
                            onSignOut = homeViewModel::onSignOutClicked,
                            onThemeChosen = ::chooseTheme,
                            onReduceMotion = ::chooseReduceMotion,
                            onForgetSavedProfile = homeViewModel::onForgetSavedProfile,
                        ),
                        admin = homeViewModel.adminState.collectAsStateWithLifecycle().value,
                        adminActions = com.turnhub.android.ui.home.AdminActions(
                            onRefresh = homeViewModel::onAdminRefresh,
                            onDeveloperRefresh = homeViewModel::onDeveloperRefresh,
                            run = homeViewModel::onAdmin,
                            onDismissMessage = homeViewModel::onAdminMessageDismissed,
                            onDownloadLog = { homeViewModel.onDownloadLog(::shareLog) },
                        ),
                        theme = theme,
                        reduceMotion = reduceMotion,
                        onSignInSubmit = homeViewModel::onSignInSubmitted,
                        onSignInDismiss = homeViewModel::onSignInDismissed,
                        onCreateAccount = homeViewModel::onCreateAccountSubmitted,
                        onAccessibilitySave = homeViewModel::onAccessibilitySaved,
                        onAccessibilityDismiss = homeViewModel::onAccessibilityDismissed,
                        setup = homeViewModel.setupState.collectAsStateWithLifecycle().value,
                        updatesAvailable = homeViewModel.updatesAvailable.collectAsStateWithLifecycle().value,
                        onOpenUpdates = homeViewModel::onUpdatesOpened,
                        seatClaim = homeViewModel.seatClaim.collectAsStateWithLifecycle().value,
                        seatActions = com.turnhub.android.ui.home.SeatActions(
                            onClaim = homeViewModel::onClaimSeat,
                            onDismiss = homeViewModel::onSeatClaimDismissed,
                        ),
                        setupActions = com.turnhub.android.ui.setup.SetupActions(
                            run = homeViewModel::onSetup,
                            dismiss = homeViewModel::onSetupDismissed,
                            close = homeViewModel::onSetupClosed,
                        ),
                    )
                }
            }
        }
    }
}

/** Android 17 (API 37), where ACCESS_LOCAL_NETWORK is enforced for apps targeting it. */
private const val LOCAL_NETWORK_PERMISSION_SDK = 37
