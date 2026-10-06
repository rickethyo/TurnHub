package com.turnhub.android.data

import com.turnhub.android.protocol.ProfileSummary
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import org.json.JSONException
import org.json.JSONObject

/** A seat as Atlas names it in `/api/v1/state` players: controller handle and slot. */
data class TabletSeat(val moduleId: Int, val slot: Int) {
    val fields: List<Pair<String, String>> get() = listOf("module" to "$moduleId", "slot" to "$slot")
}

/** A saved profile Atlas wants a PIN for before the tablet may seat it. */
data class TabletPinPrompt(val profileId: String, val name: String)

data class TabletState(
    /** Atlas showed a code and waits for it (`purpose=tablet`). */
    val codePrompt: Boolean = false,
    val busy: Boolean = false,
    /** Saved profiles for the lobby's quick-seat chips. */
    val savedProfiles: List<ProfileSummary> = emptyList(),
    val pinPrompt: TabletPinPrompt? = null,
    /** The last refusal or note, phrased for the table; cleared by the next success. */
    val message: ActionFeedback? = null,
)

/**
 * Tablet mode (protocol/http-v1.md "Tablet mode"): this device is one shared
 * screen in the middle of the table and acts for every seat through
 * `/api/tablet/*`. Atlas grants it once a signed-in account proves it is at
 * the table with a presence code, and still applies its own rules to every
 * action; the screen only learns outcomes from the next state snapshot.
 *
 * Requests are not serialized with each other (several players tap at once);
 * a refused or unconfirmed one is reported, never replayed.
 */
class AtlasTablet(
    private val session: AtlasPlayerSession,
    private val endpoint: () -> AtlasEndpoint?,
) {
    private val _state = MutableStateFlow(TabletState())
    val state: StateFlow<TabletState> = _state.asStateFlow()

    fun clearMessage() = _state.update { it.copy(message = null) }

    fun dismissPin() = _state.update { it.copy(pinPrompt = null) }

    fun forget() {
        _state.value = TabletState()
    }

    // --- the grant ----------------------------------------------------------------

    /** Asks Atlas to show a six-digit code on its screen. */
    suspend fun requestCode() {
        val response = post("/api/presence/request", listOf("purpose" to "tablet")) ?: return
        if (response.ok) _state.update { it.copy(codePrompt = true, message = null) } else fail(response)
    }

    /** Confirms the code, then turns this session into the table's tablet. */
    suspend fun confirmCode(code: String) {
        val confirm = post("/api/presence/confirm", listOf("code" to code.filter(Char::isDigit))) ?: return
        if (!confirm.ok) return fail(confirm, "That code was not accepted.")
        val enable = post("/api/tablet/enable") ?: return
        if (!enable.ok) return fail(enable)
        _state.update { it.copy(codePrompt = false, message = null) }
        session.refresh()
    }

    fun dismissCode() = _state.update { it.copy(codePrompt = false) }

    /** Hands the table back: the session stays signed in, without the tablet grant. */
    suspend fun disable() {
        post("/api/tablet/disable")
        _state.update { TabletState() }
        session.refresh()
    }

    // --- the lobby ----------------------------------------------------------------

    suspend fun loadSavedProfiles() {
        val endpoint = endpoint() ?: return
        try {
            val profiles = session.profiles(endpoint)
            _state.update { it.copy(savedProfiles = profiles) }
        } catch (_: AtlasException) {
            // The chips are a shortcut; typing the name still works.
        }
    }

    /** A new name creates a profile without a PIN and seats it. */
    suspend fun seatNew(name: String) {
        val response = post("/api/tablet/seat", listOf("name" to name.trim())) ?: return
        if (response.ok) succeed() else fail(response)
        loadSavedProfiles()
    }

    /** Seats a saved profile; Atlas asks for its PIN unless the profile allows tablet use without one. */
    suspend fun seatSaved(profileId: String, name: String, pin: String? = null) {
        val fields = buildList {
            add("profileId" to profileId)
            if (pin != null) add("pin" to pin)
        }
        _state.update { it.copy(pinPrompt = null) }
        val response = post("/api/tablet/seat", fields) ?: return
        when {
            response.ok -> succeed()
            response.code == 403 && flag(response, "pinRequired") && pin == null ->
                _state.update { it.copy(pinPrompt = TabletPinPrompt(profileId, name)) }
            else -> fail(response)
        }
    }

    suspend fun unseat(profileId: String) {
        val response = post("/api/tablet/unseat", listOf("profileId" to profileId)) ?: return
        if (response.ok) succeed() else fail(response)
    }

    /** `/api/game/settings` fields, sent for any seated player; null keeps Atlas's value. */
    suspend fun saveSettings(seat: TabletSeat, gameProfile: String?, startingLife: Int?, twoHeadedGiant: Boolean?) {
        val fields = buildList {
            addAll(seat.fields)
            gameProfile?.let { add("gameProfile" to it) }
            startingLife?.let { add("startingLife" to "$it") }
            twoHeadedGiant?.let { add("twoHeadedGiant" to if (it) "1" else "0") }
        }
        val response = post("/api/tablet/settings", fields) ?: return
        if (response.ok) succeed() else fail(response)
    }

    // --- the table ----------------------------------------------------------------

    /** `pass`, `pause`, `concede`, `win`, `confirm`, `deny`, `starter`, `start`, `cancel-start`, `rematch` or `reset`. */
    suspend fun control(seat: TabletSeat, action: String): Boolean {
        val response = post("/api/tablet/control", seat.fields + ("action" to action)) ?: return false
        if (response.ok) succeed() else fail(response)
        return response.ok
    }

    /** Changes the seat's own life by [delta]; true when Atlas applied it. */
    suspend fun life(seat: TabletSeat, delta: Int): Boolean {
        val response = post("/api/tablet/life", seat.fields + ("delta" to "$delta")) ?: return false
        if (response.ok) succeed() else fail(response)
        return response.ok
    }

    /** Commander damage the seat received from [source]'s [commander] (1 or 2). */
    suspend fun commander(seat: TabletSeat, source: Int, commander: Int, delta: Int) {
        val response = post(
            "/api/tablet/commander",
            seat.fields + listOf("source" to "$source", "commander" to "$commander", "delta" to "$delta"),
        ) ?: return
        if (response.ok) succeed() else fail(response)
    }

    /** Answers a phone's request to change this seat's life. */
    suspend fun respondLife(seat: TabletSeat, requestId: Long, accept: Boolean) {
        val response = post(
            "/api/tablet/life/respond",
            seat.fields + listOf("requestId" to "$requestId", "accept" to if (accept) "1" else "0"),
        ) ?: return
        if (response.ok) succeed() else fail(response)
    }

    // --- plumbing -----------------------------------------------------------------

    private suspend fun post(path: String, fields: List<Pair<String, String>> = emptyList()): RawResponse? {
        _state.update { it.copy(busy = true) }
        return try {
            session.raw("POST", path, fields).also {
                if (it == null) _state.update { s -> s.copy(message = ActionFeedback("Sign in first.", isError = true)) }
            }
        } catch (e: CancellationException) {
            throw e
        } catch (e: AtlasException) {
            val text = when (e.failure) {
                is AtlasFailure.Timeout, is AtlasFailure.Unreachable ->
                    "Atlas didn't confirm that. Check the table before trying again."
                else -> e.failure.userMessage
            }
            _state.update { it.copy(message = ActionFeedback(text, isError = true)) }
            null
        } finally {
            _state.update { it.copy(busy = false) }
        }
    }

    private fun succeed() = _state.update { it.copy(message = null) }

    private fun fail(response: RawResponse, fallback: String? = null) {
        val text = errorOf(response) ?: fallback ?: "Atlas refused that (HTTP ${response.code})."
        _state.update { it.copy(message = ActionFeedback(text, isError = true)) }
    }

    private fun json(response: RawResponse): JSONObject? = try {
        JSONObject(response.body)
    } catch (_: JSONException) {
        null
    }

    private fun errorOf(response: RawResponse): String? =
        json(response)?.let { it.optString("error").ifBlank { it.optString("message") } }?.ifBlank { null }

    private fun flag(response: RawResponse, key: String): Boolean = json(response)?.optBoolean(key) == true
}
