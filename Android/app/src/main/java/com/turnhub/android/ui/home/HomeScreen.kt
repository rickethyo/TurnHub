package com.turnhub.android.ui.home

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.Button
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import com.turnhub.android.protocol.AtlasConnectionState
import com.turnhub.android.ui.components.ConnectionStateBadge
import com.turnhub.android.ui.components.SigilListItem
import com.turnhub.android.ui.components.TableSummaryCard

/**
 * The app's one screen for this milestone: connection state, a mocked table
 * summary, and a mocked list of paired Sigils. Pure function of [uiState] plus
 * two callbacks -- no repository/ViewModel reference here, so this composable
 * is trivially previewable and testable on its own.
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun HomeScreen(
    uiState: HomeUiState,
    onConnectClick: () -> Unit,
    onDisconnectClick: () -> Unit,
    modifier: Modifier = Modifier,
) {
    Scaffold(
        modifier = modifier,
        topBar = { TopAppBar(title = { Text("TurnHub") }) },
    ) { innerPadding ->
        LazyColumn(
            modifier = Modifier
                .fillMaxSize()
                .padding(innerPadding),
            contentPadding = PaddingValues(16.dp),
            verticalArrangement = Arrangement.spacedBy(16.dp),
        ) {
            item {
                Column(verticalArrangement = Arrangement.spacedBy(12.dp)) {
                    ConnectionStateBadge(state = uiState.connectionState)
                    ConnectionAction(
                        connectionState = uiState.connectionState,
                        onConnectClick = onConnectClick,
                        onDisconnectClick = onDisconnectClick,
                    )
                }
            }

            val summary = uiState.tableSummary
            if (summary != null) {
                item { TableSummaryCard(summary = summary) }
            } else {
                item {
                    Text(
                        text = "Connect to an Atlas to see the table.",
                        style = MaterialTheme.typography.bodyMedium,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                    )
                }
            }

            item {
                Text(text = "Paired Sigils", style = MaterialTheme.typography.titleMedium)
            }

            if (uiState.sigils.isEmpty()) {
                item {
                    Text(
                        text = "No Sigils to show yet.",
                        style = MaterialTheme.typography.bodyMedium,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                    )
                }
            } else {
                items(uiState.sigils, key = { it.id }) { sigil ->
                    SigilListItem(sigil = sigil)
                }
            }
        }
    }
}

@Composable
private fun ConnectionAction(
    connectionState: AtlasConnectionState,
    onConnectClick: () -> Unit,
    onDisconnectClick: () -> Unit,
) {
    when (connectionState) {
        AtlasConnectionState.DISCONNECTED -> {
            Button(onClick = onConnectClick) { Text("Connect to Atlas") }
        }
        AtlasConnectionState.CONNECTING -> {
            Button(onClick = {}, enabled = false) { Text("Connecting…") }
        }
        AtlasConnectionState.CONNECTED -> {
            OutlinedButton(onClick = onDisconnectClick) { Text("Disconnect") }
        }
    }
}
