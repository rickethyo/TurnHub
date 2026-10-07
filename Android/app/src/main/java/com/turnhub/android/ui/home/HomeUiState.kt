package com.turnhub.android.ui.home

import com.turnhub.android.protocol.AccessibilitySettings
import com.turnhub.android.data.ActionFeedback
import com.turnhub.android.data.AtlasEndpoint
import com.turnhub.android.data.PlayerSessionState
import com.turnhub.android.domain.TableSummary
import com.turnhub.android.protocol.AtlasConnectionState
import com.turnhub.android.protocol.GameSettingsInfo
import com.turnhub.android.protocol.ProfileSummary
import com.turnhub.android.protocol.TableState

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
    val player: PlayerPanel? = null,
    val signIn: SignInPrompt? = null,
    /** The next match's setup, while this session may read it. */
    val gameSettings: GameSettingsInfo? = null,
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

/**
 * Playing from this phone: who is signed in and which controls make sense
 * right now. Game facts (whose turn, pending pass, pause) come from Atlas's
 * snapshot; the session only says who this phone is. Enabling a button is a
 * courtesy, not a rule: Atlas validates every control and may still refuse it.
 */
data class PlayerPanel(
    val session: PlayerSessionState = PlayerSessionState.SignedOut,
    val busy: Boolean = false,
    val feedback: ActionFeedback? = null,
    /** One line describing where this phone stands, for sighted and screen-reader users alike. */
    val status: String = "",
    val canJoin: Boolean = false,
    val canPass: Boolean = false,
    val passLabel: String = "Pass",
    val canPauseResume: Boolean = false,
    val pauseResumeLabel: String = "Pause",
    /** Shown only to the host in the lobby, when Atlas says the settings are editable. */
    val timerEditor: TurnTimerEditor? = null,
) {
    val signedIn: Boolean get() = session is PlayerSessionState.SignedIn

    companion object {
        fun from(
            summary: TableSummary,
            session: PlayerSessionState,
            busy: Boolean,
            feedback: ActionFeedback?,
            gameSettings: GameSettingsInfo? = null,
        ): PlayerPanel {
            val signedIn = session as? PlayerSessionState.SignedIn
                ?: return PlayerPanel(session = session, busy = busy, feedback = feedback)
            val info = signedIn.info
            val me = summary.players.firstOrNull { info?.participating == true && it.playerNumber == info.playerNumber }
            val name = signedIn.name ?: "this profile"
            if (me == null) {
                return PlayerPanel(
                    session = session,
                    busy = busy,
                    feedback = feedback,
                    status = if (summary.state == TableState.LOBBY) {
                        "Signed in as $name. Join the table to play from this phone."
                    } else {
                        "Signed in as $name. A game is in progress; you can join in the lobby."
                    },
                    canJoin = !busy && summary.state == TableState.LOBBY,
                )
            }

            val running = summary.state == TableState.RUNNING
            val paused = summary.state == TableState.PAUSED
            val myTurn = summary.hasTurn(me.playerNumber) && !me.eliminated
            val passPending = summary.sameTeam(summary.pending.passPlayer, me.playerNumber)
            val decisionPending = summary.pending.winClaimPlayer != null || summary.pending.eliminationTargetPlayer != null
            return PlayerPanel(
                session = session,
                busy = busy,
                feedback = feedback,
                status = buildString {
                    append("You are ${me.label}")
                    when {
                        me.eliminated -> append(" (out of this game)")
                        running && myTurn -> append(". It's your turn.")
                        paused -> append(". The game is paused.")
                        else -> append('.')
                    }
                },
                canPass = !busy && running && myTurn,
                passLabel = if (passPending) "Cancel pass" else "Pass",
                canPauseResume = !busy && !me.eliminated && (running || (paused && !decisionPending)),
                pauseResumeLabel = if (paused) "Resume" else "Pause",
                timerEditor = gameSettings
                    ?.takeIf { it.canEdit && summary.state == TableState.LOBBY }
                    ?.let {
                        TurnTimerEditor(
                            currentMs = it.settings.turnTimerMs,
                            presetsMs = it.turnTimerPresetsMs,
                            minMs = it.turnTimerMinMs,
                            maxMs = it.turnTimerMaxMs,
                        )
                    },
            )
        }
    }
}

/** The host's turn-timer choice. Values are Atlas's; custom input is checked against its range. */
data class TurnTimerEditor(
    val currentMs: Long,
    val presetsMs: List<Long>,
    val minMs: Long,
    val maxMs: Long,
) {
    val currentIsPreset: Boolean get() = currentMs in presetsMs

    /** Whole seconds within Atlas's range, or null with nothing sent. */
    fun customMs(secondsText: String): Long? {
        val seconds = secondsText.trim().toLongOrNull() ?: return null
        val ms = seconds * 1000
        return ms.takeIf { seconds > 0 && it in minMs..maxMs }
    }
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

