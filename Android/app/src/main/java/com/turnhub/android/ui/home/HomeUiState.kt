package com.turnhub.android.ui.home

import com.turnhub.android.protocol.AccessibilitySettings
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
    /** When Atlas stopped answering; [tableSummary] is then its last known state. */
    val offlineSinceMs: Long? = null,
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
    /** The phone's Wi-Fi is off: ask to turn it on before joining Atlas. */
    val wifiOff: Boolean = false,
    val homePairing: com.turnhub.android.data.SavedHomeAtlas? = null,
    val player: PlayerPanel? = null,
    val signIn: SignInPrompt? = null,
    val personalization: com.turnhub.android.data.Personalization? = null,
    /** The signed-in profile's PIN state and privacy choices. */
    val profileChoices: com.turnhub.android.data.ProfileChoices? = null,
    /** The signed-in profile's statistics, once read (or why they couldn't be). */
    val profileStats: com.turnhub.android.data.StatisticsLoad? = null,
    val avatars: List<com.turnhub.android.protocol.AvatarIcon> = emptyList(),
    /** The Sigil accessibility editor, while open. */
    val accessibility: AccessibilityPrompt? = null,
    /** What the app found nearby before joining a table. */
    val discovery: Discovery = Discovery.Idle,
    /** This phone has joined a table before; with none, the screen leads with setup. */
    val hasSavedTable: Boolean = false,
    /** Atlas is restarting (new Wi-Fi password, factory reset, update) and the app is rejoining it. */
    val rejoining: Boolean = false,
    /** The account this phone signs in to this Atlas automatically, if any (app lock). */
    val savedProfile: com.turnhub.android.data.SavedProfile? = null,
    /** This phone can keep a profile behind its fingerprint, face or screen lock (Android 11+). */
    val appLockAvailable: Boolean = false,
) {
    /** The endpoint can only be changed while nothing is open or opening. */
    val endpointEditable: Boolean
        get() = connectionState == AtlasConnectionState.DISCONNECTED && joiningSsid == null

    /** Connected, but the latest poll failed and is being retried. */
    val isRetrying: Boolean get() = connectionState == AtlasConnectionState.CONNECTED && errorMessage != null

    /** Atlas isn't answering: the table shown is its last known state, read-only. */
    val isOffline: Boolean get() = offlineSinceMs != null
}

/** On launch the app rejoins the table it knows (HomeViewModel.onAppStarted). */
sealed interface Discovery {
    /** Nothing automatic is running; Connect joins the usual way. */
    data object Idle : Discovery

    /** Rejoining the saved table. */
    data object Searching : Discovery

    /** The saved table didn't answer: switched off, out of range, or factory-reset. */
    data object NotFound : Discovery
}

/** The Atlas Wi-Fi password prompt: which network, and why we're asking. */
data class WifiPrompt(val ssid: String, val message: String)

/** Who this phone is signed in as on Atlas, and the last account action's outcome. */
data class PlayerPanel(
    val session: PlayerSessionState = PlayerSessionState.SignedOut,
    val busy: Boolean = false,
    val feedback: ActionFeedback? = null,
) {
    val signedIn: Boolean get() = session is PlayerSessionState.SignedIn
}

/**
 * Editing the signed-in player's Sigil accessibility. [settings] is Atlas's
 * copy (null while it loads or if reading failed; see [error]).
 */
data class AccessibilityPrompt(
    val settings: AccessibilitySettings? = null,
    val busy: Boolean = false,
    val error: String? = null,
)

data class SignInPrompt(
    val loading: Boolean = false,
    val profiles: List<ProfileSummary> = emptyList(),
    val submitting: Boolean = false,
    val error: String? = null,
    /** Offer "Sign in automatically on this phone", switched on. */
    val offerRemember: Boolean = false,
    val preselect: String? = null,
)

/**
 * A check the Activity runs with the phone's fingerprint, face or screen
 * lock before the vault is used: [Save] stores a secret just accepted by
 * Atlas, [Unlock] signs [profile] in automatically.
 */
sealed interface AppLockRequest {
    val profile: com.turnhub.android.data.SavedProfile

    class Save(override val profile: com.turnhub.android.data.SavedProfile, internal val secret: String) : AppLockRequest {
        override fun toString() = "Save(${profile.profileId}, secret=***)"
    }

    data class Unlock(override val profile: com.turnhub.android.data.SavedProfile) : AppLockRequest
}

