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
import androidx.compose.ui.Modifier
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.turnhub.android.data.AtlasPlayerSession
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

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContent {
            TurnHubTheme {
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
                    )
                }
            }
        }
    }
}

/** Android 17 (API 37), where ACCESS_LOCAL_NETWORK is enforced for apps targeting it. */
private const val LOCAL_NETWORK_PERMISSION_SDK = 37
