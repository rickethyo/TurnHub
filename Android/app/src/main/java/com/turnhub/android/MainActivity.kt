package com.turnhub.android

import android.Manifest
import android.content.Intent
import android.content.pm.PackageManager
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.provider.Settings
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.activity.viewModels
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.turnhub.android.data.AtlasLinkHoldService
import com.turnhub.android.data.AtlasPlayerSession
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
        )
    }

    // Android 17 blocks local-network traffic (so every Atlas request would just
    // time out) until the user grants ACCESS_LOCAL_NETWORK ("Nearby devices").
    // Asked for at the moment it's needed: when the user taps Connect.
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

    /** After a permanent denial Android shows no dialog; the user must allow it here. */
    private fun openAppSettings() {
        startActivity(
            Intent(Settings.ACTION_APPLICATION_DETAILS_SETTINGS, Uri.fromParts("package", packageName, null)),
        )
    }

    // Appearance is a phone-only preference, like the portal's per-browser theme.
    private val uiPrefs by lazy { getSharedPreferences("turnhub_ui", MODE_PRIVATE) }
    private var theme by mutableStateOf(TurnHubThemeChoice.BRASS)
    private var reduceMotion by mutableStateOf(false)

    private fun chooseTheme(choice: TurnHubThemeChoice) {
        theme = choice
        uiPrefs.edit().putString("theme", choice.key).apply()
    }

    private fun chooseReduceMotion(on: Boolean) {
        reduceMotion = on
        uiPrefs.edit().putBoolean("reduceMotion", on).apply()
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
        if (!isChangingConfigurations) AtlasLinkHoldService.release(this)
        super.onDestroy()
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        theme = TurnHubThemeChoice.fromKey(uiPrefs.getString("theme", null))
        reduceMotion = uiPrefs.getBoolean("reduceMotion", false)
        setContent {
            TurnHubTheme(choice = theme) {
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
                            onLoadPersonalization = homeViewModel::onLoadPersonalization,
                            onSavePersonalization = homeViewModel::onSavePersonalization,
                            onAccessibility = homeViewModel::onAccessibilityClicked,
                            onSignOut = homeViewModel::onSignOutClicked,
                            onThemeChosen = ::chooseTheme,
                            onReduceMotion = ::chooseReduceMotion,
                        ),
                        theme = theme,
                        reduceMotion = reduceMotion,
                        onSignInSubmit = homeViewModel::onSignInSubmitted,
                        onSignInDismiss = homeViewModel::onSignInDismissed,
                        onAccessibilitySave = homeViewModel::onAccessibilitySaved,
                        onAccessibilityDismiss = homeViewModel::onAccessibilityDismissed,
                    )
                }
            }
        }
    }
}

/** Android 17 (API 37), where ACCESS_LOCAL_NETWORK is enforced for apps targeting it. */
private const val LOCAL_NETWORK_PERMISSION_SDK = 37
