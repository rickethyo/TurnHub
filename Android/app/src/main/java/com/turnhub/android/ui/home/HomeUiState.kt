package com.turnhub.android.ui.home

import com.turnhub.android.data.AtlasEndpoint
import com.turnhub.android.domain.TableSummary
import com.turnhub.android.protocol.AtlasConnectionState

/**
 * Everything the Home screen needs to render, derived from
 * [com.turnhub.android.data.AtlasRepository] flows plus the screen's own
 * inputs. Keeping this as one immutable data class keeps [HomeScreen] a pure
 * function of state.
 */
data class HomeUiState(
    val connectionState: AtlasConnectionState = AtlasConnectionState.DISCONNECTED,
    val endpointText: String = AtlasEndpoint.DEFAULT.baseUrl,
    val tableSummary: TableSummary? = null,
    /** A failure to show the user, if any. */
    val errorMessage: String? = null,
    /** The underlying exception text for that failure, for diagnosis. */
    val errorDetail: String? = null,
    /** The failure can only be fixed from the app's system settings page. */
    val offerAppSettings: Boolean = false,
    /** The Atlas Wi-Fi being joined right now, if any. */
    val joiningSsid: String? = null,
    /** Asking the user for Atlas Wi-Fi details, if non-null. */
    val wifiPrompt: WifiPrompt? = null,
) {
    /** The endpoint can only be changed while nothing is open or opening. */
    val endpointEditable: Boolean
        get() = connectionState == AtlasConnectionState.DISCONNECTED && joiningSsid == null

    /** Connected, but the latest poll failed and is being retried. */
    val isRetrying: Boolean get() = connectionState == AtlasConnectionState.CONNECTED && errorMessage != null
}

/** The Atlas Wi-Fi password prompt: which network, and why we're asking. */
data class WifiPrompt(val ssid: String, val message: String)
