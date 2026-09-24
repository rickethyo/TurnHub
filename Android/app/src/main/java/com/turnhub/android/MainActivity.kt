package com.turnhub.android

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.viewModels
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.runtime.getValue
import androidx.compose.ui.Modifier
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.turnhub.android.data.AtlasTransportFactory
import com.turnhub.android.data.HttpAtlasRepository
import com.turnhub.android.data.HttpAtlasTransport
import com.turnhub.android.data.WifiPreferringConnectionOpener
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
        val opener = WifiPreferringConnectionOpener(applicationContext)
        val transports = AtlasTransportFactory { endpoint -> HttpAtlasTransport(endpoint, opener) }
        HomeViewModel.factory { scope -> HttpAtlasRepository(transports, scope) }
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
                        onConnectClick = homeViewModel::onConnectClicked,
                        onDisconnectClick = homeViewModel::onDisconnectClicked,
                    )
                }
            }
        }
    }
}
