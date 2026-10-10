package com.turnhub.android.data

import com.turnhub.android.domain.TableSummary
import com.turnhub.android.protocol.ProfileSummary
import com.turnhub.android.protocol.TableState
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

/**
 * A life or Commander-damage change tapped while Atlas wasn't answering. It is
 * kept for one participant (not a seat, which a controller swap can move) in
 * one game, and sent through the usual `/api/tablet/` routes once Atlas
 * answers again, where Atlas's own rules decide it like any other tap.
 */
data class OfflineChange(
    val participantId: Long,
    /** Assigned by [AtlasTablet.queueOffline]. */
    val id: Long = 0,
    /** Commander damage from this player number, or null for a life change. */
    val source: Int? = null,
    /** Which of [source]'s commanders (1 or 2). */
    val commander: Int = 1,
    val delta: Int,
    /** When it was first tapped, on [com.turnhub.android.domain.TableClock]. */
    val queuedAtMs: Long,
    /** Atlas boot and game clock it was tapped against: a restart or a new game drops it. */
    val bootId: String,
    val gameElapsedMs: Long,
    val game: Int = 1,
    /** [SENDING] while on its way; then the revision it was applied against, kept until a newer snapshot carries it. */
    val sentAtRevision: Long? = null,
) {
    val isLife: Boolean get() = source == null
    val sent: Boolean get() = sentAtRevision != null && sentAtRevision != SENDING
    fun sameTarget(other: OfflineChange) =
        game == other.game && bootId == other.bootId && participantId == other.participantId && source == other.source && commander == other.commander

    companion object {
        const val SENDING = -1L
    }
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
    /** Life and Commander changes waiting for Atlas, or sent and not yet in a snapshot. */
    val offline: List<OfflineChange> = emptyList(),
) {
    /** Life still to show for [participantId] on top of the snapshot (Commander damage costs life too). */
    fun offlineLife(participantId: Long): Int = offline.filter { it.participantId == participantId }
        .sumOf { if (it.isLife) it.delta else -it.delta }

    /** Commander damage still to show for [participantId] from [source]'s [commander]. */
    fun offlineCommander(participantId: Long, source: Int, commander: Int): Int = offline
        .filter { it.participantId == participantId && it.source == source && it.commander == commander }
        .sumOf { it.delta }

    /** Changes not yet sent. */
    val waiting: Int get() = offline.count { it.sentAtRevision == null }
}

/**
 * Tablet mode (protocol/http-v1.md "Tablet mode"): this device is one shared
 * screen in the middle of the table and acts for every seat through
 * the `/api/tablet/` routes. Atlas grants it once a signed-in account proves it is at
 * the table with a presence code, and still applies its own rules to every
 * action; the screen only learns outcomes from the next state snapshot.
 *
 * Requests are not serialized with each other (several players tap at once);
 * a refused or unconfirmed one is reported, never replayed.
 */
class AtlasTablet(
    private val session: AtlasPlayerSession,
    private val endpoint: () -> AtlasEndpoint?,
) : TableControls {
    private val _state = MutableStateFlow(TabletState())
    val state: StateFlow<TabletState> = _state.asStateFlow()
    private var nextOfflineId = 1L

    /** Bind requests to the table rendered by the originating screen. */
    suspend fun inGame(game: Int, block: suspend AtlasTablet.() -> Unit) =
        kotlinx.coroutines.withContext(ExpectedGame(game)) { block() }

    fun clearMessage() = _state.update { it.copy(message = null) }

    fun dismissPin() = _state.update { it.copy(pinPrompt = null) }

    fun forget() {
        _state.value = TabletState()
    }

    // --- the grant ----------------------------------------------------------------

    /**
     * Turns tablet mode on. With the table code off (Atlas's default) that
     * works at once; otherwise Atlas shows a six-digit code on its screen.
     */
    suspend fun requestCode() {
        val direct = post("/api/tablet/enable") ?: return
        if (direct.ok) {
            _state.update { it.copy(codePrompt = false, message = null) }
            session.refresh()
            return
        }
        val response = post("/api/presence/request", listOf("purpose" to "tablet")) ?: return
        if (response.ok) _state.update { it.copy(codePrompt = true, message = null) } else fail(response)
    }

    /** Confirms the code, then turns this session into the table's tablet. */
    suspend fun confirmCode(code: String) {
        val confirm = post("/api/presence/confirm", listOf("code" to code.filter(Char::isDigit))) ?: return
        if (!confirm.ok) return fail(confirm, "That code was not accepted.")
        enable()
    }

    /** Turns this session into the table's tablet; alone, for an account with the Tablet access role. */
    suspend fun enable() {
        val enable = post("/api/tablet/enable") ?: return
        if (!enable.ok) return fail(enable)
        _state.update { it.copy(codePrompt = false, message = null) }
        session.refresh()
    }

    fun dismissCode() = _state.update { it.copy(codePrompt = false) }

    /** End the shared tablet credential and leave this device signed out. */
    override suspend fun disable() {
        try { post("/api/tablet/disable") }
        finally { session.forget(); _state.value = TabletState() }
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
    override suspend fun control(seat: TabletSeat, action: String): Boolean {
        val response = post("/api/tablet/control", seat.fields + ("action" to action)) ?: return false
        if (response.ok) succeed() else fail(response)
        return response.ok
    }

    /** Changes the seat's own life by [delta]; true when Atlas applied it. */
    override suspend fun life(seat: TabletSeat, delta: Int): Boolean {
        val response = post("/api/tablet/life", seat.fields + ("delta" to "$delta")) ?: return false
        if (response.ok) succeed() else fail(response)
        return response.ok
    }

    /** Commander damage the seat received from [source]'s [commander] (1 or 2). */
    override suspend fun commander(seat: TabletSeat, source: Int, commander: Int, delta: Int) {
        val response = post(
            "/api/tablet/commander",
            seat.fields + listOf("source" to "$source", "commander" to "$commander", "delta" to "$delta"),
        ) ?: return
        if (response.ok) succeed() else fail(response)
    }

    /** Answers a phone's request to change this seat's life. */
    override suspend fun respondLife(seat: TabletSeat, requestId: Long, accept: Boolean) {
        val response = post(
            "/api/tablet/life/respond",
            seat.fields + listOf("requestId" to "$requestId", "accept" to if (accept) "1" else "0"),
        ) ?: return
        if (response.ok) succeed() else fail(response)
    }

    // --- while Atlas isn't answering --------------------------------------------------

    /**
     * Keeps a life or Commander-damage change for when Atlas answers again.
     * Taps on the same target add up into one change, keeping the first tap's
     * time; a change that adds up to nothing is dropped.
     */
    override fun queueOffline(change: OfflineChange) = _state.update { state ->
        val index = state.offline.indexOfFirst { it.sentAtRevision == null && it.sameTarget(change) }
        val offline = if (index < 0) {
            state.offline + change.copy(id = nextOfflineId++, sentAtRevision = null)
        } else {
            val merged = state.offline[index].let { it.copy(delta = it.delta + change.delta) }
            if (merged.delta == 0) state.offline.filterIndexed { i, _ -> i != index }
            else state.offline.mapIndexed { i, it -> if (i == index) merged else it }
        }
        state.copy(offline = offline)
    }

    /** Refuses an action that only Atlas can decide live (passing, pausing, answers…). */
    override fun refuseOffline() = _state.update {
        it.copy(message = ActionFeedback(
            "Atlas is offline. Life and Commander damage are saved for it; this waits until it's back.",
            isError = true,
        ))
    }

    /**
     * Sends the waiting changes, in the order they were tapped, now that Atlas
     * answers with [summary]. Each is checked against it first: one from before
     * an Atlas restart, from a game that has since ended or been replaced, or for
     * a player no longer in the game is dropped. Atlas applies the rest under its
     * usual rules. Quiet when everything lands; otherwise one note says what didn't.
     */
    suspend fun replayOffline(summary: TableSummary, nowMs: Long) {
        val waiting = _state.value.offline.filter { it.sentAtRevision == null }.sortedBy { it.queuedAtMs }
        if (waiting.isEmpty()) return
        // Taken out of the queue first, so a tap made while these are sent never merges into one.
        val ids = waiting.map { it.id }.toSet()
        _state.update { state ->
            state.copy(offline = state.offline.map { if (it.id in ids) it.copy(sentAtRevision = OfflineChange.SENDING) else it })
        }
        val playing = summary.state == TableState.RUNNING || summary.state == TableState.PAUSED
        var dropped = 0
        var refused = 0
        for (change in waiting) {
            val player = summary.players.firstOrNull { it.participantId == change.participantId }
            val sameGame = playing && change.game == summary.game && change.bootId == summary.bootId && summary.gameElapsedMs >= change.gameElapsedMs
            val source = change.source
            val sourceThere = source == null || summary.players.any { it.playerNumber == source }
            if (!sameGame || player == null || player.eliminated || !sourceThere) {
                dropped++
                removeOffline(change)
                continue
            }
            val seat = TabletSeat(player.controller.id, player.slot)
            val age = listOf("queuedMs" to "${(nowMs - change.queuedAtMs).coerceIn(0, 86_400_000)}")
            val response = if (source == null) {
                post("/api/tablet/life", seat.fields + ("delta" to "${change.delta}") + age)
            } else {
                post(
                    "/api/tablet/commander",
                    seat.fields + listOf("source" to "$source", "commander" to "${change.commander}", "delta" to "${change.delta}") + age,
                )
            }
            if (response?.ok == true) {
                _state.update { state ->
                    state.copy(offline = state.offline.map { if (it.id == change.id) it.copy(sentAtRevision = summary.revision) else it })
                }
            } else {
                refused++
                removeOffline(change)
            }
        }
        val missed = dropped + refused
        _state.update {
            it.copy(message = if (missed == 0) null else ActionFeedback(
                if (missed == 1) "1 change made while Atlas was offline wasn't applied: the game or player had changed, or Atlas refused it."
                else "$missed changes made while Atlas was offline weren't applied: the game or players had changed, or Atlas refused them.",
                isError = true,
            ))
        }
    }

    /** Forgets sent changes once a snapshot newer than the one they were sent against arrives. */
    fun settleOffline(revision: Long) = _state.update { state ->
        if (state.offline.none { it.sent && it.sentAtRevision != revision }) state
        else state.copy(offline = state.offline.filter { !it.sent || it.sentAtRevision == revision })
    }

    /** Drops everything waiting (Atlas restarted, or the device left the table). */
    fun clearOffline() = _state.update { it.copy(offline = emptyList()) }

    private fun removeOffline(change: OfflineChange) =
        _state.update { state -> state.copy(offline = state.offline.filterNot { it.id == change.id }) }

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

/** Per-coroutine presentation context; concurrent seat taps cannot overwrite it. */
class ExpectedGame(val game: Int) : kotlin.coroutines.AbstractCoroutineContextElement(Key) {
    companion object Key : kotlin.coroutines.CoroutineContext.Key<ExpectedGame>
}
