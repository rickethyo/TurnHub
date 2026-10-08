package com.turnhub.android.data

import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import org.json.JSONArray
import org.json.JSONException
import org.json.JSONObject

/** `/api/presence`: whether this account is verified at the table (see protocol/http-v1.md). */
/** `/api/presence`. [codeRequired] false (Atlas's default) means no table code is asked for. */
data class PresenceInfo(
    val verified: Boolean,
    val remainingMs: Long,
    val setup: Boolean,
    val canRequest: Boolean,
    val codeRequired: Boolean = true,
)

/** `/api/network`. The password itself is never returned. */
data class NetworkInfo(
    val ssid: String,
    val security: String,
    val passwordIsDefault: Boolean,
    val passwordLength: Int,
    val stations: Int,
)

/** One paired Sigil from `/api/devices`. */
data class DeviceInfo(
    val id: Int,
    val label: String,
    val defaultLabel: String,
    val customName: String,
    val hardwareId: String,
    val online: Boolean,
    val firmware: String,
    val display: String,
    val sessionCount: Int,
    /** Hello capability bits (shared/include/protocol.h); 0x80 marks a test harness. */
    val capabilities: Int = 0,
) {
    val isHarness: Boolean get() = capabilities and CAPABILITY_HARNESS != 0

    companion object {
        const val CAPABILITY_HARNESS = 0x80
    }
}

/**
 * One account from `/api/accounts`. The last four fields let the People sheet
 * explain a blocked action before Atlas refuses it: roles and moderation need
 * a PIN, the initial Admin keeps Admin, and archiving waits until the account
 * has left the table.
 */
data class AccountInfo(
    val profileId: String,
    val name: String,
    val permissions: Int,
    val archived: Boolean,
    val avatar: Int,
    val nudgeMuted: Boolean,
    val hasPin: Boolean = true,
    val primary: Boolean = false,
    val atTable: Boolean = false,
    val reconnectRequired: Boolean = false,
) {
    fun has(permission: com.turnhub.android.protocol.AccountPermission): Boolean = permissions and permission.bit != 0
}

/** A Sigil waiting for an Admin to compare its pairing code (`pendingPairings` in `/api/devices`). */
data class PendingPairing(val id: Int, val code: String, val secondsLeft: Int)

data class ActivityEvent(val ageMs: Long, val kind: String, val message: String)

/**
 * Everything the Device Settings and Developer screens show. Each part is
 * null until read; reads the account may not make (403) leave it null.
 */
data class AdminState(
    val presence: PresenceInfo? = null,
    val network: NetworkInfo? = null,
    val atlasHardwareId: String? = null,
    val devices: List<DeviceInfo> = emptyList(),
    val pendingPairings: List<PendingPairing> = emptyList(),
    val pairingWindowMs: Long? = null,
    val pairingChoicesMs: List<Long> = emptyList(),
    val speakerVolume: Int? = null,
    val accounts: List<AccountInfo> = emptyList(),
    /** Developer: the activity feed and raw JSON panels. */
    val activity: List<ActivityEvent> = emptyList(),
    val devPanels: Map<String, String> = emptyMap(),
    /** A presence code is showing on the Atlas screen; the UI asks for it. */
    val codePrompt: Boolean = false,
    val busy: Boolean = false,
    val message: ActionFeedback? = null,
)

/**
 * Settings that restart Atlas. The screen that owns the connection remembers
 * the Wi-Fi password Atlas comes back with and rejoins by itself, so the app
 * reopens the table instead of dropping to the connect screen.
 */
interface AtlasRestarts {
    suspend fun wifiPasswordChanged(password: String)

    /** Atlas comes back as new, on the printed default password. */
    suspend fun factoryReset()

    companion object {
        val NONE = object : AtlasRestarts {
            override suspend fun wifiPasswordChanged(password: String) = Unit
            override suspend fun factoryReset() = Unit
        }
    }
}

/**
 * The admin and developer side of the portal (Device Settings, account
 * permissions, Game Master moderation, the Developer page), over the same
 * routes. Atlas checks every permission and the table-presence code; this
 * class only shows what Atlas returns. A request Atlas answers with
 * `403 {"presenceRequired": true}` asks Atlas to show a code, and is retried
 * once after [confirmCode] succeeds, as the portal does.
 */
class AtlasAdminConsole(
    private val session: AtlasPlayerSession,
    private val restarts: AtlasRestarts = AtlasRestarts.NONE,
) {

    private val _state = MutableStateFlow(AdminState())
    val state: StateFlow<AdminState> = _state.asStateFlow()

    private var pendingRetry: (suspend () -> Unit)? = null

    fun clearMessage() = _state.update { it.copy(message = null) }

    fun forget() {
        pendingRetry = null
        _state.value = AdminState()
    }

    /** Re-reads what the account may see. */
    suspend fun refresh(admin: Boolean, gameMaster: Boolean) {
        get("/api/presence")?.let { d ->
            _state.update {
                it.copy(
                    presence = parsePresence(d),
                )
            }
        }
        if (admin) {
            get("/api/network")?.let { d ->
                _state.update {
                    it.copy(
                        network = NetworkInfo(
                            d.optString("ssid"),
                            d.optString("security"),
                            d.optBoolean("passwordIsDefault"),
                            d.optInt("passwordLength"),
                            d.optInt("stations"),
                        ),
                    )
                }
            }
            refreshDevices()
            get("/api/pairing")?.let { d ->
                _state.update {
                    it.copy(
                        pairingWindowMs = d.optLong("windowMs"),
                        pairingChoicesMs = d.optJSONArray("choicesMs").longs(),
                    )
                }
            }
            get("/api/speaker")?.let { d -> _state.update { it.copy(speakerVolume = d.optInt("volume")) } }
        }
        if (admin || gameMaster) {
            get("/api/accounts")?.let { d ->
                val list = d.optJSONArray("accounts").objects().map { x ->
                    AccountInfo(
                        profileId = x.optString("profileId"),
                        name = x.optString("name"),
                        permissions = x.optInt("permissions"),
                        archived = x.optBoolean("archived"),
                        avatar = x.optInt("avatar"),
                        nudgeMuted = x.optBoolean("nudgeMuted"),
                        hasPin = x.optBoolean("hasPin", true),
                        primary = x.optBoolean("primary"),
                        atTable = x.optBoolean("atTable"),
                        reconnectRequired = x.optBoolean("reconnectRequired"),
                    )
                }.sortedWith(compareBy<AccountInfo>({ it.archived }, { !it.atTable }, { it.name.lowercase() }))
                _state.update { it.copy(accounts = list) }
            }
        }
    }

    /** Paired Sigils and pairing requests; the Sigils card polls this while it is shown. */
    suspend fun refreshDevices() {
        get("/api/devices")?.let { d ->
            val list = d.optJSONArray("devices").objects().map { x ->
                DeviceInfo(
                    id = x.optInt("id"),
                    label = x.optString("label"),
                    defaultLabel = x.optString("defaultLabel"),
                    customName = x.optString("customName"),
                    hardwareId = x.optString("hardwareId"),
                    online = x.optBoolean("online"),
                    firmware = x.optString("firmware"),
                    display = x.optString("display"),
                    sessionCount = x.optInt("sessionCount"),
                    capabilities = x.optInt("capabilities"),
                )
            }
            val pending = d.optJSONArray("pendingPairings").objects().map {
                PendingPairing(it.optInt("id"), it.optString("code"), it.optInt("secondsLeft"))
            }
            _state.update {
                it.copy(devices = list, pendingPairings = pending, atlasHardwareId = d.optJSONObject("atlas")?.optString("hardwareId"))
            }
        }
    }

    /** Developer page: activity feed plus the raw status, devices, seats and diagnostics JSON. */
    suspend fun refreshDeveloper() {
        get("/api/diagnostics/activity")?.let { d ->
            val events = d.optJSONArray("events").objects().map {
                ActivityEvent(it.optLong("ageMs"), it.optString("kind"), it.optString("message"))
            }
            _state.update { it.copy(activity = events) }
        }
        val panels = linkedMapOf<String, String>()
        for (name in listOf("status", "devices", "seats", "diagnostics")) {
            val response = raw("GET", "/api/$name") ?: continue
            panels[name] = if (response.ok) pretty(response.body) else "HTTP ${response.code}"
        }
        _state.update { it.copy(devPanels = panels) }
    }

    /** The RAM serial log as text, or null with the reason in [AdminState.message]. */
    suspend fun downloadLog(): String? {
        val response = raw("GET", "/api/diagnostics/log") ?: return null
        if (!response.ok) {
            fail(errorOf(response) ?: "Could not download the serial log")
            return null
        }
        return response.body
    }

    // --- table presence ---------------------------------------------------------

    /** Asks Atlas to show a six-digit code; the UI then asks the player for it. */
    suspend fun requestCode(): Boolean {
        val response = raw("POST", "/api/presence/request") ?: return false
        if (!response.ok) {
            pendingRetry = null
            fail(errorOf(response) ?: "Atlas could not show a code")
            return false
        }
        _state.update { it.copy(codePrompt = true, message = null) }
        return true
    }

    suspend fun confirmCode(code: String) {
        val response = raw("POST", "/api/presence/confirm", listOf("code" to code.filter(Char::isDigit))) ?: return
        if (!response.ok) {
            fail(errorOf(response) ?: "That code was not accepted")
            return
        }
        _state.update { it.copy(codePrompt = false, message = ActionFeedback("Verified at the table for 10 minutes.", false)) }
        refreshPresence()
        pendingRetry?.let { retry ->
            pendingRetry = null
            retry()
        }
    }

    fun dismissCode() {
        pendingRetry = null
        _state.update { it.copy(codePrompt = false) }
    }

    /**
     * Turns the table code on or off (`/api/table-code`). Off is Atlas's
     * default; turning it off again asks for a code while it is on.
     */
    suspend fun setTableCodeRequired(required: Boolean) = protectedPost(
        "/api/table-code",
        listOf("required" to if (required) "1" else "0"),
        success = if (required) "Table code turned on." else "Table code turned off.",
    ) { refreshPresence() }

    suspend fun lockPresence() {
        post("/api/presence/lock", success = "Verification ended.")
        refreshPresence()
    }

    /** First-Admin setup: verify at the table, then make this account the Admin. */
    suspend fun setupAdmin() {
        protectedPost("/api/accounts/setup", success = "Admin account established.")
    }

    // --- device settings ---------------------------------------------------------

    suspend fun saveNetworkPassword(password: String) = protectedPost(
        "/api/network/password",
        listOf("password" to password),
        success = "Password saved. Atlas is restarting; the app rejoins with the new password.",
    ) { restarts.wifiPasswordChanged(password) }

    suspend fun renameDevice(id: Int, name: String) =
        protectedPost("/api/device/name", listOf("module" to id.toString(), "name" to name),
            success = if (name.isEmpty()) "Custom name cleared." else "Sigil renamed.")

    suspend fun forgetDevice(id: Int?) =
        post("/api/device/forget", if (id == null) listOf("all" to "1") else listOf("module" to id.toString()),
            success = "Sigil forgotten.")

    suspend fun factoryResetSigil(id: Int) =
        protectedPost("/api/device/factory-reset", listOf("module" to id.toString()), success = "Factory reset sent to the Sigil.")

    suspend fun factoryResetAtlas() = protectedPost(
        "/api/device/factory-reset",
        listOf("atlas" to "1"),
        success = "Atlas is restarting as new. The app rejoins it, then setup starts again.",
    ) { restarts.factoryReset() }

    suspend fun resetTable() =
        protectedPost("/api/table/reset", success = "The table is back to an empty lobby.")

    suspend fun savePairingWindow(ms: Long) =
        post("/api/pairing", listOf("windowMs" to ms.toString()), success = "Pairing window saved.")

    suspend fun saveSpeakerVolume(volume: Int) =
        post("/api/speaker", listOf("volume" to volume.toString()), success = "Speaker volume saved.")

    /** The Table and Atlas card's one Save: pairing window, then speaker volume; stops at the first refusal. */
    suspend fun saveTableSettings(pairingWindowMs: Long?, speakerVolume: Int?) {
        if (pairingWindowMs != null) {
            val response = send("/api/pairing", listOf("windowMs" to pairingWindowMs.toString())) ?: return
            if (!response.ok) return finish(response, "")
        }
        if (speakerVolume != null) {
            val response = send("/api/speaker", listOf("volume" to speakerVolume.toString())) ?: return
            if (!response.ok) return finish(response, "")
        }
        _state.update { it.copy(message = ActionFeedback("Atlas settings saved.", isError = false)) }
    }

    // --- accounts ---------------------------------------------------------------

    suspend fun savePermissions(profileId: String, permissions: Int) =
        post("/api/accounts/permissions", listOf("profileId" to profileId, "permissions" to permissions.toString()),
            success = "Roles saved.")

    /** Answers a Sigil's pairing request after comparing codes; Atlas may ask for table presence first. */
    suspend fun answerPairing(id: Int, accept: Boolean) = protectedPost(
        "/api/device/pair-confirm",
        listOf("module" to id.toString(), "accept" to if (accept) "1" else "0"),
        success = if (accept) "Sigil paired securely." else "Pairing rejected.",
    ) { refreshDevices() }

    suspend fun archive(profileId: String, archived: Boolean) =
        post("/api/accounts/archive", listOf("profileId" to profileId, "archived" to if (archived) "1" else "0"),
            success = if (archived) "Account archived." else "Account restored.")

    /** Game Master: `pass`, `mute`, `unmute`, `reset` or `remove`. [success] names what happened. */
    suspend fun moderate(profileId: String, action: String, success: String = "Moderation applied.") =
        post("/api/accounts/moderate", listOf("profileId" to profileId, "action" to action), success = success)

    // --- plumbing ---------------------------------------------------------------

    private suspend fun refreshPresence() {
        get("/api/presence")?.let { d ->
            _state.update {
                it.copy(presence = parsePresence(d))
            }
        }
    }

    /** A POST Atlas may gate on table presence: on 403 presenceRequired, show a code and retry once verified. */
    /** [then] runs only after Atlas accepts (for example, rejoining after a restart). */
    private fun parsePresence(d: JSONObject) = PresenceInfo(
        d.optBoolean("verified"),
        d.optLong("remainingMs"),
        d.optBoolean("setup"),
        d.optBoolean("canRequest"),
        d.optBoolean("codeRequired", true),
    )

    private suspend fun protectedPost(
        path: String,
        fields: List<Pair<String, String>> = emptyList(),
        success: String,
        then: (suspend () -> Unit)? = null,
    ) {
        val response = send(path, fields) ?: return
        if (response.code == 403 && presenceRequired(response)) {
            pendingRetry = { post(path, fields, success, then) }
            requestCode()
            return
        }
        finish(response, success)
        if (response.ok) then?.invoke()
    }

    private suspend fun post(
        path: String,
        fields: List<Pair<String, String>> = emptyList(),
        success: String,
        then: (suspend () -> Unit)? = null,
    ) {
        val response = send(path, fields) ?: return
        finish(response, success)
        if (response.ok) then?.invoke()
    }

    private suspend fun send(path: String, fields: List<Pair<String, String>>): RawResponse? {
        _state.update { it.copy(busy = true) }
        return try {
            raw("POST", path, fields)
        } finally {
            _state.update { it.copy(busy = false) }
        }
    }

    private fun finish(response: RawResponse, success: String) {
        if (response.ok) {
            val message = try {
                JSONObject(response.body).optString("message").takeIf { it.isNotBlank() }
            } catch (_: JSONException) {
                null
            }
            _state.update { it.copy(message = ActionFeedback(message ?: success, isError = false)) }
        } else {
            fail(errorOf(response) ?: "Atlas refused that (HTTP ${response.code}).")
        }
    }

    private fun fail(message: String) {
        _state.update { it.copy(message = ActionFeedback(message, isError = true)) }
    }

    private suspend fun get(path: String): JSONObject? {
        val response = raw("GET", path) ?: return null
        if (!response.ok) return null
        return try {
            JSONObject(response.body)
        } catch (_: JSONException) {
            null
        }
    }

    private suspend fun raw(method: String, path: String, fields: List<Pair<String, String>> = emptyList()): RawResponse? =
        try {
            session.raw(method, path, fields)
        } catch (e: CancellationException) {
            throw e
        } catch (e: AtlasException) {
            fail(e.failure.userMessage)
            null
        }

    private fun presenceRequired(response: RawResponse): Boolean = try {
        JSONObject(response.body).optBoolean("presenceRequired")
    } catch (_: JSONException) {
        false
    }

    private fun errorOf(response: RawResponse): String? = try {
        JSONObject(response.body).optString("error").takeIf { it.isNotBlank() }
    } catch (_: JSONException) {
        null
    }

    private fun pretty(body: String): String = try {
        JSONObject(body).toString(2)
    } catch (_: JSONException) {
        body
    }

    private fun JSONArray?.objects(): List<JSONObject> =
        if (this == null) emptyList() else List(length()) { optJSONObject(it) }.filterNotNull()

    private fun JSONArray?.longs(): List<Long> =
        if (this == null) emptyList() else List(length()) { optLong(it) }
}
