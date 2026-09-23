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
import com.turnhub.android.data.AtlasRepository
import com.turnhub.android.data.MockAtlasRepository
import com.turnhub.android.ui.home.HomeScreen
import com.turnhub.android.ui.home.HomeViewModel
import com.turnhub.android.ui.theme.TurnHubTheme

/**
 * Single-Activity host for this milestone's one screen. A later milestone may
 * introduce navigation between lobby/game/settings screens; see Android/README.md's
 * proposed source layout (`ui/lobby`, `ui/game`, `ui/player`, `ui/settings`).
 */
class MainActivity : ComponentActivity() {

    // Manual, minimal composition root: one repository instance for the life of
    // the Activity. The app is intentionally small enough that a DI framework
    // is not yet justified -- revisit once a real repository has its own
    // dependencies (an HTTP client, credentials storage, etc).
    private val repository: AtlasRepository by lazy { MockAtlasRepository() }

    private val homeViewModel: HomeViewModel by viewModels {
        HomeViewModel.factory(repository)
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
                        onConnectClick = homeViewModel::onConnectClicked,
                        onDisconnectClick = homeViewModel::onDisconnectClicked,
                    )
                }
            }
        }
    }
}
