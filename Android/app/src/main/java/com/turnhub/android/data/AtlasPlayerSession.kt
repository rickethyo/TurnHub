package com.turnhub.android.data

import com.turnhub.android.protocol.AccessibilitySettings
import com.turnhub.android.protocol.LedStyle
import com.turnhub.android.protocol.ProfileSummary
import com.turnhub.android.protocol.SessionInfo
import kotlinx.coroutines.CancellationException
import org.json.JSONObject
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock

/** Who this phone is signed in as on Atlas, if anyone. */
sealed interface PlayerSessionState {
    data object SignedOut : PlayerSessionState

    /** [info] is Atlas's latest reading of the session (profile and permissions). */
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

/** The outcome of the last action, phrased for the player. */
data class ActionFeedback(val message: String, val isError: Boolean)

/**
 * This phone's account on Atlas through its session routes: sign in to a
 * profile with its PIN, then read and change that profile's records and
 * settings. Games run on the phone; Atlas keeps the accounts. An action that
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

    /** The signed-in player's Sigil accessibility preferences, once read with [loadAccessibility]. */
    private val _accessibility = MutableStateFlow<AccessibilitySettings?>(null)
    val accessibility: StateFlow<AccessibilitySettings?> = _accessibility.asStateFlow()

    /** PIN state and privacy choices, once read with [loadChoices]. */
    private val _choices = MutableStateFlow<ProfileChoices?>(null)
    val choices: StateFlow<ProfileChoices?> = _choices.asStateFlow()

    private val mutex = Mutex()
    @Volatile private var endpoint: AtlasEndpoint? = null
    @Volatile private var token: String? = null

    /** Public profile list for the sign-in picker. Throws [AtlasException]. */
    suspend fun profiles(endpoint: AtlasEndpoint): List<ProfileSummary> =
        call { transports.create(endpoint).getProfiles() }

    /** Profile sync (no sign-in: the request carries the profile's link key). */
    suspend fun syncProfile(endpoint: AtlasEndpoint, fields: List<Pair<String, String>>): RawResponse = call {
        transports.create(endpoint).raw("POST", "/api/profiles/sync", "", fields)
    }

    suspend fun avatarPresets(endpoint: AtlasEndpoint): List<com.turnhub.android.protocol.AvatarIcon> =
        call { transports.create(endpoint).getAvatars() }

    /** An independent verification session; leaves the active table login unchanged. */
    suspend fun verifyLocalAccount(endpoint: AtlasEndpoint, player: com.turnhub.android.standalone.SavedLocalPlayer,
        targetId: String?, secret: String): LocalAccountTransfer.Result = call {
        LocalAccountTransfer(transports.create(endpoint)).verify(player, targetId, secret)
    }

    /** A separate, short-lived profile verification; never replaces the hub's own session. */
    suspend fun attachRoomProfile(target: AtlasEndpoint, request: JSONObject): String? = call {
        val transport = transports.create(target)
        val offered = request.optJSONObject("profile")
        val id = request.optString("profileId").ifBlank { offered?.optString("profileId").orEmpty() }
        if (!com.turnhub.android.standalone.LocalProfileIds.valid(id)) return@call null
        val known = transport.getProfiles().firstOrNull { it.profileId == id }
        val secret = request.optString("pin")
        if (known?.hasPin == true && secret.isBlank()) return@call null
        val login = if (known != null) transport.login(id, secret) else {
            if (offered == null || offered.optString("profileId") != id) return@call null
            val response = transport.raw("POST", "/api/profiles/register", "",
                listOf("profileId" to id, "name" to offered.getString("name"), "pin" to secret, "gameplayOnly" to "1"))
            if (!response.ok) return@call null
            if (secret.isBlank()) null else com.turnhub.android.protocol.AtlasWireParser.parseLogin(response.body)
        }
        try {
            // PIN verification enables the durable sync link. Room credentials remain separately scoped.
            if (login != null && secret.isNotBlank()) {
                val linked = transport.raw("POST", "/api/profiles/link-key", login.token)
                if (!linked.ok) return@call null
                val link = JSONObject(linked.body).put("atlasId", request.getString("_atlasId"))
                request.put("_profileLink", link)
                if (offered != null && offered.optString("profileId") == id) {
                    val look = offered.optJSONObject("look")
                    val avatar = transport.getAvatars().firstOrNull { it.key == look?.optString("avatar") }?.id ?: 0
                    val sync = transport.raw("POST", "/api/profiles/sync", "", listOf("profileId" to id,
                        "linkKey" to link.getString("linkKey"), "now" to "${System.currentTimeMillis()}",
                        "activity" to "${offered.optLong("activity").coerceAtLeast(1)}",
                        "createdAtMs" to "${offered.optLong("createdAtMs")}", "name" to offered.getString("name"),
                        "color" to (look?.optString("color")?.takeIf { it.startsWith("#") } ?: "none"),
                        "avatar" to "$avatar", "reduceMotion" to if (look?.optBoolean("reduceMotion") == true) "1" else "0"))
                    if (!sync.ok) return@call null
                    val updated = JSONObject(sync.body)
                    val key = transport.getAvatars().firstOrNull { it.id == updated.optInt("avatar") }?.key ?: "none"
                    request.put("_profile", JSONObject().put("profileId", id).put("name", updated.optString("name"))
                        .put("activity", updated.optLong("activity")).put("createdAtMs", updated.optLong("createdAtMs"))
                        .put("look", com.turnhub.android.standalone.LocalPersonalization(
                            color = updated.optString("color").takeIf { it.startsWith("#") }, avatar = key,
                            reduceMotion = updated.optBoolean("reduceMotion")).toJson()))
                }
            }
            id
        } finally {
            if (login != null) kotlinx.coroutines.withContext(kotlinx.coroutines.NonCancellable) { transport.logout(login.token) }
        }
    }

    /** One page of a linked profile's match records on Atlas (no sign-in: the link key). */
    suspend fun profileHistory(endpoint: AtlasEndpoint, profileId: String, linkKey: String, skip: Int): RawResponse = call {
        transports.create(endpoint).raw("GET", "/api/profiles/history?profileId=$profileId&linkKey=$linkKey&skip=$skip&limit=20", "")
    }

    /** Reads an Atlas account to recreate on this phone; leaves the active table login unchanged. */
    suspend fun restoreAccount(endpoint: AtlasEndpoint, profileId: String, secret: String): LocalAccountTransfer.Restored = call {
        LocalAccountTransfer(transports.create(endpoint)).restore(profileId, secret)
    }

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


    /** A new display name and/or PIN for the signed-in profile. */
    suspend fun saveProfile(name: String?, pin: String?, currentPin: String? = null) = act {
        val message = transports.create(it.first).saveProfile(it.second, name, pin, currentPin)
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

    /** Removes the profile's PIN or password after checking [currentPin]; Atlas refuses it for accounts with a role. */
    suspend fun clearPin(currentPin: String) {
        act { formPost(it, "/api/session/profile", listOf("clearPin" to "1", "currentPin" to currentPin), "PIN removed.") }
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
    private val _uploadProgress = MutableStateFlow<Float?>(null)
    /** 0..1 while an image upload to Atlas runs; null otherwise. */
    val uploadProgress: StateFlow<Float?> = _uploadProgress.asStateFlow()

    private val _avatars = MutableStateFlow<List<com.turnhub.android.protocol.AvatarIcon>>(emptyList())
    val avatars: StateFlow<List<com.turnhub.android.protocol.AvatarIcon>> = _avatars.asStateFlow()

    suspend fun loadPersonalization() = act {
        val transport = transports.create(it.first)
        if (_avatars.value.isEmpty()) _avatars.value = transport.getAvatars()
        _personalization.value = artworkImages(transport, it.second, transport.getPersonalization(it.second))
        null
    }

    private suspend fun artworkImages(transport: AtlasSessionTransport, token: String, value: Personalization): Personalization {
        suspend fun image(path: String): ByteArray? = if (path.isBlank()) null else try {
            transport.getAvatarImage(path, token)
        } catch (e: CancellationException) { throw e } catch (_: Exception) { null }
        return value.copy(customImage = image(value.customAvatar), pendingImage = image(value.pendingAvatar))
    }

    suspend fun uploadAvatar(bytes: ByteArray, thumbnail: String) = act {
        require(bytes.size in 32..48 * 1024)
        val transport = transports.create(it.first)
        suspend fun post(fields: List<Pair<String, String>>): org.json.JSONObject {
            val response = transport.raw("POST", "/api/session/avatar", it.second, fields)
            if (!response.ok) throw AtlasException(AtlasFailure.Rejected(
                com.turnhub.android.protocol.AtlasWireParser.errorMessage(response.body) ?: "Image upload failed"))
            return org.json.JSONObject(response.body)
        }
        val chunks = bytes.asList().chunked(768)
        // Start, each chunk and submit are the steps the progress bar counts.
        val steps = chunks.size + 2f
        _uploadProgress.value = 0f
        val revision = try {
            val started = post(listOf("action" to "start", "size" to bytes.size.toString())).getString("revision")
            _uploadProgress.value = 1 / steps
            chunks.forEachIndexed { index, chunk ->
                val hex = chunk.joinToString("") { byte -> "%02x".format(byte.toInt() and 255) }
                post(listOf("action" to "chunk", "revision" to started, "index" to index.toString(), "data" to hex))
                _uploadProgress.value = (index + 2) / steps
            }
            post(listOf("action" to "submit", "revision" to started, "thumbnail" to thumbnail))
            started
        } finally {
            _uploadProgress.value = null
        }
        _personalization.value = artworkImages(transport, it.second, transport.getPersonalization(it.second))
        ActionFeedback(if (_personalization.value?.pendingAvatar?.contains("&revision=$revision") == true)
            "Image saved on Atlas. An Admin must approve it before it appears at the table."
        else "Image saved and available at the local table.", false)
    }

    suspend fun removeAvatar() = act {
        val transport = transports.create(it.first)
        val result = transport.raw("POST", "/api/session/avatar", it.second, listOf("action" to "remove"))
        if (!result.ok) throw AtlasException(AtlasFailure.Rejected(
            com.turnhub.android.protocol.AtlasWireParser.errorMessage(result.body) ?: "Could not remove image"))
        _personalization.value = transport.getPersonalization(it.second)
        ActionFeedback("Uploaded image removed.", false)
    }

    suspend fun savePersonalization(color: String?, avatar: Int?) = act {
        val transport = transports.create(it.first)
        _personalization.value = artworkImages(transport, it.second, transport.savePersonalization(it.second, color, avatar))
        ActionFeedback("Saved. Your Sigil and the table update within a few seconds.", isError = false)
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
    suspend fun avatarImage(path: String): ByteArray? {
        val endpoint = endpoint ?: return null
        val token = token ?: return null
        return try { transports.create(endpoint).getAvatarImage(path, token) }
        catch (e: CancellationException) { throw e } catch (_: Exception) { null }
    }

    suspend fun raw(method: String, path: String, fields: List<Pair<String, String>> = emptyList()): RawResponse? =
        rawRequest(method, path, fields)

    /** [AtlasTransport.rawText] for a signed-in session; null when signed out. */
    suspend fun rawText(path: String, fields: List<Pair<String, String>>, body: String): RawResponse? {
        val endpoint = endpoint ?: return null
        val token = token ?: return null
        val response = call { transports.create(endpoint).rawText(path, token, fields, body) }
        if (response.code == 401) {
            clear()
            _feedback.value = ActionFeedback(AtlasFailure.SessionExpired.userMessage, isError = true)
        }
        return response
    }

    private suspend fun rawRequest(method: String, path: String, fields: List<Pair<String, String>>): RawResponse? {
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
        _accessibility.value = null
        _personalization.value = null
        _choices.value = null
        _stats.value = null
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

    private companion object {
        const val CLAIM_POLL_MS = 450L
    }
}
