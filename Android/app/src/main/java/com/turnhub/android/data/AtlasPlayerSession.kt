package com.turnhub.android.data

import com.turnhub.android.protocol.ProfileSummary
import com.turnhub.android.protocol.SessionInfo
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock

/** Who this phone is signed in as on Atlas, if anyone. */
sealed interface PlayerSessionState {
    data object SignedOut : PlayerSessionState

    /** [info] is Atlas's latest resolution of the session (seat, player, host, active). */
    data class SignedIn(val profileId: String, val name: String?, val info: SessionInfo?) : PlayerSessionState
}

/** The outcome of the last action, phrased for the player. */
data class ActionFeedback(val message: String, val isError: Boolean)

/**
 * Lets this phone act as a player through Atlas's session routes: sign in to a
 * profile with its PIN, join the table, and send PASS / pause-resume.
 *
 * Atlas stays authoritative: it resolves the player from the session, applies
 * its own rules and the app only learns the result from the next state
 * snapshot. Controls carry the snapshot's revision and boot ID, so a tap made
 * against stale state is refused (CONFLICT) instead of applied. A control that
 * times out is reported, never replayed (protocol/http-v1.md). Actions are
 * serialized; a tap while another action is in flight is ignored.
 *
 * The session token lives only in memory. Atlas sessions are RAM-only, so
 * [forget] must be called when the connection ends or Atlas's boot changes.
 */
class AtlasPlayerSession(private val transports: AtlasSessionTransportFactory) {

    private val _state = MutableStateFlow<PlayerSessionState>(PlayerSessionState.SignedOut)
    val state: StateFlow<PlayerSessionState> = _state.asStateFlow()

    private val _busy = MutableStateFlow(false)
    val busy: StateFlow<Boolean> = _busy.asStateFlow()

    private val _feedback = MutableStateFlow<ActionFeedback?>(null)
    val feedback: StateFlow<ActionFeedback?> = _feedback.asStateFlow()

    private val mutex = Mutex()
    private var endpoint: AtlasEndpoint? = null
    private var token: String? = null

    /** Public profile list for the sign-in picker. Throws [AtlasException]. */
    suspend fun profiles(endpoint: AtlasEndpoint): List<ProfileSummary> =
        call { transports.create(endpoint).getProfiles() }

    /** Signs in; throws [AtlasException] with Atlas's reason (e.g. wrong PIN, throttled). */
    suspend fun signIn(endpoint: AtlasEndpoint, profile: ProfileSummary, pin: String) {
        mutex.withLock {
            val transport = transports.create(endpoint)
            val login = call { transport.login(profile.profileId, pin) }
            val info = try {
                call { transport.me(login.token) }
            } catch (_: AtlasException) {
                null // Signed in regardless; seat info arrives on the next refresh.
            }
            this.endpoint = endpoint
            token = login.token
            _feedback.value = null
            _state.value = PlayerSessionState.SignedIn(login.profileId, info?.name ?: profile.name, info)
        }
    }

    /** Re-reads `/api/session/me` (after state changes). Quietly signs out if Atlas dropped the session. */
    suspend fun refresh() {
        if (mutex.isLocked) return
        mutex.withLock { refreshLocked() }
    }

    suspend fun join() = act {
        val message = transports.create(it.first).join(it.second)
        ActionFeedback(message ?: "Joined the table.", isError = false)
    }

    suspend fun control(action: ControlAction, expectedRevision: Long?, expectedBootId: String?) = act {
        val result = transports.create(it.first).control(it.second, action, expectedRevision, expectedBootId)
        when {
            result.ok -> ActionFeedback(result.message ?: "Done.", isError = false)
            result.status == "CONFLICT" ->
                ActionFeedback("The table changed before Atlas got that. Check it and try again.", isError = true)
            else -> ActionFeedback(result.message ?: "Atlas refused that.", isError = true)
        }
    }

    /** Revokes the token on Atlas (best effort) and forgets it here. */
    suspend fun signOut() {
        mutex.withLock {
            val current = endpoint to token
            clear()
            val (endpoint, token) = current
            if (endpoint != null && token != null) {
                try {
                    transports.create(endpoint).logout(token)
                } catch (e: CancellationException) {
                    throw e
                } catch (_: Exception) {
                    // Atlas will expire it; nothing else to do.
                }
            }
        }
    }

    /** Drops the session locally, e.g. on disconnect or a new Atlas boot (sessions are RAM-only there). */
    fun forget() {
        clear()
    }

    fun clearFeedback() {
        _feedback.value = null
    }

    private fun clear() {
        endpoint = null
        token = null
        _state.value = PlayerSessionState.SignedOut
    }

    /** Runs one authenticated action: serialized, never retried, outcome -> [feedback]. */
    private suspend fun act(block: suspend (Pair<AtlasEndpoint, String>) -> ActionFeedback) {
        if (!mutex.tryLock()) return // Another action is in flight: ignore the extra tap.
        try {
            val endpoint = endpoint ?: return
            val token = token ?: return
            _busy.value = true
            _feedback.value = try {
                call { block(endpoint to token) }
            } catch (e: AtlasException) {
                when (e.failure) {
                    AtlasFailure.SessionExpired -> {
                        clear()
                        ActionFeedback(e.failure.userMessage, isError = true)
                    }
                    is AtlasFailure.Timeout, is AtlasFailure.Unreachable ->
                        // Ambiguous: it may or may not have happened. Never replay.
                        ActionFeedback("Atlas didn't confirm that. Check the table before trying again.", isError = true)
                    else -> ActionFeedback(e.failure.userMessage, isError = true)
                }
            }
            refreshLocked()
        } finally {
            _busy.value = false
            mutex.unlock()
        }
    }

    private suspend fun refreshLocked() {
        val endpoint = endpoint ?: return
        val token = token ?: return
        val signedIn = _state.value as? PlayerSessionState.SignedIn ?: return
        try {
            val info = call { transports.create(endpoint).me(token) }
            _state.value = signedIn.copy(name = info.name ?: signedIn.name, info = info)
        } catch (e: AtlasException) {
            if (e.failure == AtlasFailure.SessionExpired) {
                clear()
                _feedback.value = ActionFeedback(e.failure.userMessage, isError = true)
            }
            // Other failures: keep the last known info; the state poll reports connectivity.
        }
    }

    private suspend fun <T> call(block: suspend () -> T): T = try {
        block()
    } catch (e: CancellationException) {
        throw e
    } catch (e: AtlasException) {
        throw e
    } catch (e: Exception) {
        throw AtlasException(AtlasFailure.Unexpected("${e.javaClass.simpleName}: ${e.message}"))
    }
}
