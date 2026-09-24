package com.turnhub.android.ui.home

import com.turnhub.android.data.ActionFeedback
import com.turnhub.android.data.AtlasEndpoint
import com.turnhub.android.data.PlayerSessionState
import com.turnhub.android.domain.TableSummary
import com.turnhub.android.protocol.AtlasConnectionState
import com.turnhub.android.protocol.ProfileSummary

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
    val player: PlayerPanel? = null,
    val signIn: SignInPrompt? = null,
) {
    /** The endpoint can only be changed while nothing is open or opening. */
    val endpointEditable: Boolean
        get() = connectionState == AtlasConnectionState.DISCONNECTED && joiningSsid == null

    /** Connected, but the latest poll failed and is being retried. */
    val isRetrying: Boolean get() = connectionState == AtlasConnectionState.CONNECTED && errorMessage != null
}

/** The Atlas Wi-Fi password prompt: which network, and why we're asking. */
data class WifiPrompt(val ssid: String, val message: String)

data class PlayerPanel(
    val session: PlayerSessionState = PlayerSessionState.SignedOut,
    val busy: Boolean = false,
    val feedback: ActionFeedback? = null,
) {
    companion object {
        fun from(
            _tableSummary: TableSummary,
            session: PlayerSessionState,
            busy: Boolean,
            feedback: ActionFeedback?,
        ): PlayerPanel = PlayerPanel(
            session = session,
            busy = busy,
            feedback = feedback,
        )
    }
}

data class SignInPrompt(
    val loading: Boolean = false,
    val profiles: List<ProfileSummary> = emptyList(),
    val submitting: Boolean = false,
    val error: String? = null,
)

