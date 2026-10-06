package com.turnhub.android.data

import com.turnhub.android.protocol.AccessibilitySettings
import com.turnhub.android.protocol.GameSettingsInfo
import com.turnhub.android.protocol.LedStyle
import com.turnhub.android.protocol.ProfileSummary
import com.turnhub.android.protocol.SessionInfo
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.delay
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

/**
 * The signed-in profile's own choices from `/api/session/me`: whether it has a
 * PIN or password, and its privacy policy (null fields when Atlas could not
 * read the policy).
 */
data class ProfileChoices(
    val hasPin: Boolean,
    val allowPhysicalWithoutPin: Boolean?,
    val hideStatsWithoutAuthentication: Boolean?,
)

/** A Link phone seat claim: [seat] names it ("Sigil 2 seat A"); [waiting] while Atlas waits for the press. */
data class SeatClaim(val seat: String, val message: String, val waiting: Boolean, val isError: Boolean)

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

    /** The next match's setup, read only while this session is the table host. */
    private val _gameSettings = MutableStateFlow<GameSettingsInfo?>(null)
    val gameSettings: StateFlow<GameSettingsInfo?> = _gameSettings.asStateFlow()

    /** The signed-in player's Sigil accessibility preferences, once read with [loadAccessibility]. */
    private val _accessibility = MutableStateFlow<AccessibilitySettings?>(null)
    val accessibility: StateFlow<AccessibilitySettings?> = _accessibility.asStateFlow()

    /** PIN state and privacy choices, once read with [loadChoices]. */
    private val _choices = MutableStateFlow<ProfileChoices?>(null)
    val choices: StateFlow<ProfileChoices?> = _choices.asStateFlow()

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
            refreshGameSettings(transport, login.token, info)
        }
    }

    /** Creates an account and signs in to it, like [signIn]; throws [AtlasException] with Atlas's reason. */
    suspend fun register(endpoint: AtlasEndpoint, name: String, pin: String) {
        mutex.withLock {
            val transport = transports.create(endpoint)
            val login = call { transport.register(name, pin) }
            val info = try {
                call { transport.me(login.token) }
            } catch (_: AtlasException) {
                null
            }
            this.endpoint = endpoint
            token = login.token
            _feedback.value = null
            _state.value = PlayerSessionState.SignedIn(login.profileId, info?.name ?: name, info)
        }
    }

    /** The Link phone seat claim in progress or just finished, if any. */
    private val _claim = MutableStateFlow<SeatClaim?>(null)
    val claim: StateFlow<SeatClaim?> = _claim.asStateFlow()

    /**
     * Claims a physical Sigil seat, as the portal's "Use this seat": Atlas asks
     * the Sigil, and pressing Link phone there proves the player is at it.
     * Signed out, that signs this phone in to the seat's profile (only one
     * without a PIN); signed in, it attaches the Sigil seat to this profile.
     * Polls `/api/session/poll` until Atlas approves, refuses or expires it.
     */
    suspend fun claimSeat(endpoint: AtlasEndpoint, moduleId: Int, slot: Int, seatName: String) {
        if (_claim.value?.waiting == true) return
        val transport = transports.create(endpoint)
        val requester = token.takeIf { this.endpoint == endpoint }.orEmpty()
        fun done(message: String, isError: Boolean) {
            _claim.value = SeatClaim(seatName, message, waiting = false, isError = isError)
        }
        _claim.value = SeatClaim(seatName, "Asking $seatName…", waiting = true, isError = false)
        try {
            val request = call {
                transport.raw("POST", "/api/session/request", requester, listOf("module" to "$moduleId", "slot" to "$slot"))
            }
            val body = jsonOrNull(request.body)
            val requestId = body?.optString("requestId").orEmpty()
            if (!request.ok || requestId.isEmpty()) {
                return done(body?.optString("error")?.ifBlank { null } ?: "Atlas refused that seat.", isError = true)
            }
            val expiresMs = body?.optLong("expiresMs", 30_000L) ?: 30_000L
            _claim.value = SeatClaim(
                seatName,
                "On $seatName, choose Link phone within ${expiresMs / 1000} seconds.",
                waiting = true,
                isError = false,
            )
            // The portal polls about twice a second; a little slack past expiry catches a late press.
            repeat(((expiresMs + 3_000L) / CLAIM_POLL_MS).toInt()) {
                delay(CLAIM_POLL_MS)
                val poll = try {
                    call { transport.raw("GET", "/api/session/poll?id=$requestId", "") }
                } catch (_: AtlasException) {
                    return@repeat // One lost poll isn't the end; keep asking until the deadline.
                }
                val d = jsonOrNull(poll.body)
                if (!poll.ok) {
                    return done(d?.optString("error")?.ifBlank { null } ?: "The request expired. Try again.", isError = true)
                }
                if (d?.optString("status") != "approved") return@repeat
                val newToken = d?.optString("token").orEmpty()
                if (newToken.isEmpty()) return done("Atlas approved the seat but sent no sign-in.", isError = true)
                mutex.withLock {
                    val info = call { transport.me(newToken) }
                    val profileId = info.profileId
                    if (profileId.isEmpty()) {
                        return done("That seat has no profile to sign in to.", isError = true)
                    }
                    this.endpoint = endpoint
                    token = newToken
                    _feedback.value = null
                    _state.value = PlayerSessionState.SignedIn(profileId, info.name, info)
                    refreshGameSettings(transport, newToken, info)
                }
                return done("Linked. This phone now controls $seatName.", isError = false)
            }
            done("Nobody chose Link phone in time. Try again.", isError = true)
        } catch (e: AtlasException) {
            done(e.failure.userMessage, isError = true)
        } finally {
            // Cancelled mid-wait (the screen went away): don't leave a claim spinning.
            if (_claim.value?.waiting == true) _claim.value = null
        }
    }

    fun clearClaim() {
        if (_claim.value?.waiting != true) _claim.value = null
    }

    private fun jsonOrNull(body: String): org.json.JSONObject? = try {
        org.json.JSONObject(body)
    } catch (_: org.json.JSONException) {
        null
    }

    /** `GET /api/setup`; no sign-in needed. Throws [AtlasException]. */
    suspend fun setupStatus(endpoint: AtlasEndpoint): com.turnhub.android.protocol.SetupStatus =
        call { transports.create(endpoint).getSetup() }

    /** A firmware package upload as the signed-in account; null when signed out. */
    suspend fun upload(path: String, field: String, fileName: String, bytes: ByteArray): RawResponse? {
        val endpoint = endpoint ?: return null
        val token = token ?: return null
        val response = call { transports.create(endpoint).upload(path, token, field, fileName, bytes) }
        if (response.code == 401) {
            clear()
            _feedback.value = ActionFeedback(AtlasFailure.SessionExpired.userMessage, isError = true)
        }
        return response
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

    /**
     * A counter or game control with form fields (life, life requests,
     * Commander damage). Atlas resolves the player from the session.
     */
    suspend fun counter(path: String, fields: List<Pair<String, String>>, success: String) = act {
        val result = transports.create(it.first).postControl(it.second, path, fields)
        when {
            result.ok -> ActionFeedback(result.message ?: success, isError = false)
            else -> ActionFeedback(result.message ?: "Atlas refused that.", isError = true)
        }
    }

    /** Any seated player, in the lobby; null fields keep Atlas's values. */
    suspend fun saveGameSettings(
        gameProfile: String?,
        startingLife: Int?,
        turnTimerMs: Long?,
        twoHeadedGiant: Boolean? = null,
    ) = act {
        val message = transports.create(it.first)
            .saveGameSettings(it.second, gameProfile, startingLife, turnTimerMs, twoHeadedGiant)
        ActionFeedback(message ?: "Game settings saved on Atlas.", isError = false)
    }

    /** A new display name and/or PIN for the signed-in profile. */
    suspend fun saveProfile(name: String?, pin: String?) = act {
        val message = transports.create(it.first).saveProfile(it.second, name, pin)
        if (name != null) {
            (_state.value as? PlayerSessionState.SignedIn)?.let { s -> _state.value = s.copy(name = name) }
        }
        ActionFeedback(message ?: if (pin != null) "PIN saved." else "Name saved.", isError = false)
    }

    /** Reads [choices]; a failure leaves the previous value. */
    suspend fun loadChoices() {
        val response = try {
            raw("GET", "/api/session/me")
        } catch (e: CancellationException) {
            throw e
        } catch (_: AtlasException) {
            null
        } ?: return
        if (!response.ok) return
        try {
            val d = org.json.JSONObject(response.body)
            val policy = d.optBoolean("policyAvailable")
            _choices.value = ProfileChoices(
                hasPin = d.optBoolean("hasPin"),
                allowPhysicalWithoutPin = if (policy) d.optBoolean("allowPhysicalWithoutPin") else null,
                hideStatsWithoutAuthentication = if (policy) d.optBoolean("hideStatsWithoutAuthentication") else null,
            )
        } catch (_: org.json.JSONException) {
            // Keep what we had.
        }
    }

    /** Saves the profile's privacy choices (`/api/session/policy`). */
    suspend fun savePolicy(allowPhysicalWithoutPin: Boolean, hideStatsWithoutAuthentication: Boolean) {
        act {
            formPost(it, "/api/session/policy", listOf(
                "allowPhysicalWithoutPin" to if (allowPhysicalWithoutPin) "1" else "0",
                "hideStatsWithoutAuthentication" to if (hideStatsWithoutAuthentication) "1" else "0",
            ), "Privacy choices saved on Atlas.")
        }
        loadChoices()
    }

    /** Removes the profile's PIN or password; Atlas refuses it for accounts with a role. */
    suspend fun clearPin() {
        act { formPost(it, "/api/session/profile", listOf("clearPin" to "1"), "PIN removed.") }
        loadChoices()
    }

    /** The signed-in profile's statistics, once read with [loadStats]. */
    private val _stats = MutableStateFlow<StatisticsLoad?>(null)
    val stats: StateFlow<StatisticsLoad?> = _stats.asStateFlow()

    /** Reads `/api/session/stats` into [stats]; a failure keeps the last numbers and says why. */
    suspend fun loadStats() {
        val response = try {
            raw("GET", "/api/session/stats")
        } catch (e: CancellationException) {
            throw e
        } catch (e: AtlasException) {
            _stats.value = StatisticsLoad(_stats.value?.stats, e.failure.userMessage)
            return
        } ?: return
        val parsed = if (response.ok) ProfileStatistics.parse(response.body) else null
        _stats.value = if (parsed != null) {
            StatisticsLoad(parsed, null)
        } else {
            StatisticsLoad(_stats.value?.stats, errorText(response) ?: "Could not load statistics.")
        }
    }

    /**
     * The shareable text report (`/api/session/stats/export`, never including
     * moderation history), or null with the reason in [stats]' error.
     */
    suspend fun exportStats(): String? {
        val response = try {
            raw("GET", "/api/session/stats/export")
        } catch (e: CancellationException) {
            throw e
        } catch (e: AtlasException) {
            _stats.value = StatisticsLoad(_stats.value?.stats, e.failure.userMessage)
            return null
        } ?: return null
        if (!response.ok) {
            _stats.value = StatisticsLoad(_stats.value?.stats, errorText(response) ?: "Could not export statistics.")
            return null
        }
        return response.body
    }

    private fun errorText(response: RawResponse): String? = try {
        org.json.JSONObject(response.body).optString("error").ifBlank { null }
    } catch (_: org.json.JSONException) {
        null
    }

    private suspend fun formPost(
        session: Pair<AtlasEndpoint, String>,
        path: String,
        fields: List<Pair<String, String>>,
        success: String,
    ): ActionFeedback {
        val response = transports.create(session.first).raw("POST", path, session.second, fields)
        val message = try {
            org.json.JSONObject(response.body).let { if (response.ok) it.optString("message") else it.optString("error") }
        } catch (_: org.json.JSONException) {
            ""
        }
        return when {
            response.code == 401 -> throw AtlasException(AtlasFailure.SessionExpired)
            response.ok -> ActionFeedback(message.ifBlank { success }, isError = false)
            else -> ActionFeedback(message.ifBlank { "Atlas refused that (HTTP ${response.code})." }, isError = true)
        }
    }

    /** The profile's avatar and Sigil light color, once read with [loadPersonalization]. */
    private val _personalization = MutableStateFlow<Personalization?>(null)
    val personalization: StateFlow<Personalization?> = _personalization.asStateFlow()

    private val _avatars = MutableStateFlow<List<com.turnhub.android.protocol.AvatarIcon>>(emptyList())
    val avatars: StateFlow<List<com.turnhub.android.protocol.AvatarIcon>> = _avatars.asStateFlow()

    suspend fun loadPersonalization() = act {
        val transport = transports.create(it.first)
        if (_avatars.value.isEmpty()) _avatars.value = transport.getAvatars()
        _personalization.value = transport.getPersonalization(it.second)
        null
    }

    suspend fun savePersonalization(color: String?, avatar: Int?) = act {
        _personalization.value = transports.create(it.first).savePersonalization(it.second, color, avatar)
        ActionFeedback("Saved. Your Sigil and the table update within a few seconds.", isError = false)
    }

    /** Host only, in the lobby: Atlas validates and stores it for the next match. */
    suspend fun setTurnTimer(turnTimerMs: Long) = act {
        val message = transports.create(it.first).setTurnTimer(it.second, turnTimerMs)
        ActionFeedback(message ?: "Turn timer saved on Atlas.", isError = false)
    }

    /** Reads the profile's Sigil accessibility preferences from Atlas into [accessibility]. */
    suspend fun loadAccessibility() = act {
        _accessibility.value = transports.create(it.first).getAccessibility(it.second)
        null
    }

    /** Atlas validates, stores them with the profile and restyles the player's Sigil. */
    suspend fun saveAccessibility(sigilSound: Boolean, ledStyle: LedStyle, longPressMs: Int, winHoldMs: Int, lifeApprovalMs: Int) = act {
        _accessibility.value = transports.create(it.first)
            .saveAccessibility(it.second, sigilSound, ledStyle, longPressMs, winHoldMs, lifeApprovalMs)
        ActionFeedback("Sigil accessibility saved. Your Sigil updates within a few seconds.", isError = false)
    }

    /**
     * Tells Atlas the newest firmware versions (public route; no sign-in
     * needed). Returns how many devices Atlas counts as behind, or null if the
     * report didn't go through.
     */
    suspend fun reportLatestFirmware(endpoint: AtlasEndpoint, fields: List<Pair<String, String>>): Int? =
        try {
            call { transports.create(endpoint).reportLatestFirmware(fields) }
        } catch (_: AtlasException) {
            null
        }

    /**
     * One authenticated request for the admin and developer screens. Not
     * serialized with player actions (they are reads and deliberate admin
     * taps); an expired session signs out as usual. Null when signed out.
     */
    suspend fun raw(method: String, path: String, fields: List<Pair<String, String>> = emptyList()): RawResponse? {
        val endpoint = endpoint ?: return null
        val token = token ?: return null
        val response = call { transports.create(endpoint).raw(method, path, token, fields) }
        if (response.code == 401) {
            clear()
            _feedback.value = ActionFeedback(AtlasFailure.SessionExpired.userMessage, isError = true)
        }
        return response
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
        _gameSettings.value = null
        _accessibility.value = null
        _personalization.value = null
        _choices.value = null
        _stats.value = null
        if (_claim.value?.waiting != true) _claim.value = null
        _state.value = PlayerSessionState.SignedOut
    }

    /** Runs one authenticated action: serialized, never retried, outcome -> [feedback] (null clears it). */
    private suspend fun act(block: suspend (Pair<AtlasEndpoint, String>) -> ActionFeedback?) {
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
            val transport = transports.create(endpoint)
            val info = call { transport.me(token) }
            _state.value = signedIn.copy(name = info.name ?: signedIn.name, info = info)
            refreshGameSettings(transport, token, info)
        } catch (e: AtlasException) {
            if (e.failure == AtlasFailure.SessionExpired) {
                clear()
                _feedback.value = ActionFeedback(e.failure.userMessage, isError = true)
            }
            // Other failures: keep the last known info; the state poll reports connectivity.
        }
    }

    /**
     * Settings only feed the next-game editor (any seated player; Atlas still
     * reports it as `host`), so a failed read just hides it. An
     * ended session is reported by the next `/api/session/me`, not from here.
     */
    private suspend fun refreshGameSettings(transport: AtlasSessionTransport, token: String, info: SessionInfo?) {
        _gameSettings.value = if (info?.host == true && info.participating) {
            try {
                call { transport.getGameSettings(token) }
            } catch (_: AtlasException) {
                null
            }
        } else {
            null
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

    private companion object {
        const val CLAIM_POLL_MS = 450L
    }
}
