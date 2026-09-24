package com.turnhub.android.ui.home

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.semantics.LiveRegionMode
import androidx.compose.ui.semantics.liveRegion
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.unit.dp
import com.turnhub.android.protocol.AtlasConnectionState
import com.turnhub.android.ui.components.ConnectionStateBadge
import com.turnhub.android.ui.components.PhysicalSigilRow
import com.turnhub.android.ui.components.TableSummaryCard

/**
 * The app's one screen for this milestone: Atlas address, connection state, and
 * the live table as Atlas last reported it. Pure function of [uiState] plus
 * callbacks -- no repository/ViewModel reference here.
 */
@OptIn(ExperimentalMaterial3Api::class)
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
) {
    uiState.wifiPrompt?.let { prompt ->
        WifiPasswordDialog(
            prompt = prompt,
            onSubmit = onWifiPasswordSubmit,
            onUseCurrentWifi = onUseCurrentWifi,
            onDismiss = onWifiPromptDismiss,
        )
    }
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
                    OutlinedTextField(
                        value = uiState.endpointText,
                        onValueChange = onEndpointChange,
                        enabled = uiState.endpointEditable,
                        singleLine = true,
                        label = { Text("Atlas address") },
                        supportingText = { Text("TurnHub joins the Atlas Wi-Fi for you when you connect.") },
                        keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Uri, imeAction = ImeAction.Go),
                        keyboardActions = KeyboardActions(onGo = { onConnectClick() }),
                        modifier = Modifier.fillMaxWidth(),
                    )
                    ConnectionAction(
                        connectionState = uiState.connectionState,
                        joiningSsid = uiState.joiningSsid,
                        onConnectClick = onConnectClick,
                        onDisconnectClick = onDisconnectClick,
                    )
                }
            }

            uiState.errorMessage?.let { message ->
                item {
                    ErrorCard(
                        message = message,
                        detail = uiState.errorDetail,
                        retrying = uiState.isRetrying,
                        onOpenAppSettings = onOpenAppSettings.takeIf { uiState.offerAppSettings },
                    )
                }
            }

            val summary = uiState.tableSummary
            if (summary == null) {
                item {
                    Text(
                        text = "Connect to an Atlas to see the table.",
                        style = MaterialTheme.typography.bodyMedium,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                    )
                }
            } else {
                item { TableSummaryCard(summary = summary) }

                item {
                    Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
                        Text(text = "Physical Sigils at this table", style = MaterialTheme.typography.titleMedium)
                        Text(
                            text = "Only Sigils seated in the current table are listed. " +
                                "Paired Sigils that are not seated are not reported by Atlas.",
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant,
                        )
                    }
                }
                if (summary.physicalSigils.isEmpty()) {
                    item {
                        Text(
                            text = "No physical Sigils are seated.",
                            style = MaterialTheme.typography.bodyMedium,
                            color = MaterialTheme.colorScheme.onSurfaceVariant,
                        )
                    }
                } else {
                    items(summary.physicalSigils, key = { it.controller.id }) { sigil ->
                        PhysicalSigilRow(sigil = sigil)
                    }
                }
            }
        }
    }
}

@Composable
private fun ErrorCard(
    message: String,
    detail: String?,
    retrying: Boolean,
    onOpenAppSettings: (() -> Unit)?,
) {
    Card(
        colors = CardDefaults.cardColors(
            containerColor = MaterialTheme.colorScheme.errorContainer,
            contentColor = MaterialTheme.colorScheme.onErrorContainer,
        ),
        modifier = Modifier
            .fillMaxWidth()
            .semantics { liveRegion = LiveRegionMode.Polite },
    ) {
        Column(modifier = Modifier.padding(16.dp)) {
            Text(
                text = if (retrying) "Atlas not responding – retrying" else "Problem",
                style = MaterialTheme.typography.titleSmall,
            )
            Text(text = message, style = MaterialTheme.typography.bodyMedium)
            detail?.let {
                Text(text = it, style = MaterialTheme.typography.bodySmall, modifier = Modifier.padding(top = 4.dp))
            }
            onOpenAppSettings?.let { open ->
                OutlinedButton(onClick = open, modifier = Modifier.padding(top = 8.dp)) {
                    Text("Open app settings")
                }
            }
        }
    }
}

@Composable
private fun ConnectionAction(
    connectionState: AtlasConnectionState,
    joiningSsid: String?,
    onConnectClick: () -> Unit,
    onDisconnectClick: () -> Unit,
) {
    when (connectionState) {
        AtlasConnectionState.DISCONNECTED -> {
            if (joiningSsid != null) {
                Button(onClick = {}, enabled = false) { Text("Joining $joiningSsid Wi-Fi…") }
            } else {
                Button(onClick = onConnectClick) { Text("Connect to Atlas") }
            }
        }
        AtlasConnectionState.CONNECTING -> {
            Button(onClick = {}, enabled = false) { Text("Connecting…") }
        }
        AtlasConnectionState.CONNECTED -> {
            OutlinedButton(onClick = onDisconnectClick) { Text("Disconnect") }
        }
    }
}
