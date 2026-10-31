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

/** Tablet control actions for Atlas's Planechase ([AtlasTablet.control]). */
const val PLANAR_PREFIX = "planar:"

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

    /** Legacy shared-display session activation; current app controls need no activation. */
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

    suspend fun seatLocal(player: com.turnhub.android.standalone.SavedLocalPlayer, atlasId: String) {
        val linked = player.links.firstOrNull { it.atlasId == atlasId }?.profileId ?: player.profileId
        val known = _state.value.savedProfiles.any { it.profileId == linked }
        if (!known) {
            val uploaded = post("/api/profiles/register", listOf("profileId" to linked, "name" to player.name, "pin" to "", "gameplayOnly" to "1")) ?: return
            if (!uploaded.ok && uploaded.code != 409) return fail(uploaded)
        }
        seatSaved(linked, player.name)
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

    suspend fun unseat(profileId: String): Boolean {
        val response = post("/api/tablet/unseat", listOf("profileId" to profileId)) ?: return false
        if (response.ok) succeed() else fail(response)
        return response.ok
    }

    /** Uses Atlas's existing validation; a refusal stops the batch and stays visible. */
    suspend fun clearSeats(profileIds: List<String>) {
        for (id in profileIds.distinct()) if (!unseat(id)) break
    }

    /** `/api/game/settings` fields, sent for any seated player; null keeps Atlas's value. */
    suspend fun saveSettings(
        seat: TabletSeat,
        gameProfile: String?,
        startingLife: Int?,
        twoHeadedGiant: Boolean?,
        turnTimerMs: Long? = null,
        houseRules: Boolean? = null,
        planechase: Boolean? = null,
        planarDigital: Boolean? = null,
    ) {
        val fields = buildList {
            addAll(seat.fields)
            planarDigital?.let { add("planarDeck" to if (it) "digital" else "physical") }
            planechase?.let { add("planechase" to if (it) "1" else "0") }
            turnTimerMs?.let { add("turnTimerMs" to "$it") }
            houseRules?.let { add("houseRules" to if (it) "1" else "0") }
            gameProfile?.let { add("gameProfile" to it) }
            startingLife?.let { add("startingLife" to "$it") }
            twoHeadedGiant?.let { add("twoHeadedGiant" to if (it) "1" else "0") }
        }
        val response = post("/api/tablet/settings", fields) ?: return
        if (response.ok) succeed() else fail(response)
    }

    /** A seat's commander: saved to its profile on Atlas's card and chosen; null clears the choice. */
    suspend fun chooseCommander(seat: TabletSeat, name: String?, printing: String?) {
        val fields = seat.fields + if (name.isNullOrBlank()) listOf("slot" to "0")
            else listOf("name" to name.trim().take(com.turnhub.android.standalone.StandaloneGame.MAX_COMMANDER)) + (printing?.let { listOf("art" to it) } ?: emptyList())
        val response = post("/api/tablet/commander-choice", fields) ?: return
        if (response.ok) succeed() else fail(response)
    }

    /** A profile's saved commanders on Atlas (name, printing); empty without a card or any saved. */
    suspend fun savedCommanders(profileId: String): List<Pair<String, String?>> = try {
        endpoint()?.let { session.ensureGameAccess(it) }
        val response = session.raw("GET", "/api/profiles/commanders", listOf("profileId" to profileId))
        if (response == null || !response.ok) emptyList() else org.json.JSONObject(response.body).getJSONArray("commanders").let { list ->
            (0 until list.length()).map { i -> list.getJSONObject(i).let { it.getString("name") to it.optString("art").takeIf { a -> a.isNotEmpty() && a != "null" } } }
        }
    } catch (e: CancellationException) { throw e } catch (_: Exception) { emptyList() }

    /**
     * Sends the app's downloaded planes for Atlas's digital Planechase deck when Atlas's
     * catalog differs (first connection, or after the app downloads newer planes).
     */
    suspend fun syncPlanes(cards: List<com.turnhub.android.data.PlaneInfo>) {
        if (cards.isEmpty()) return
        val version = "${cards.size}-" + Integer.toHexString(cards.map { it.id }.sorted().hashCode())
        try {
            val info = session.raw("GET", "/api/planes") ?: return
            if (info.ok && org.json.JSONObject(info.body).optString("version") == version) return
            val lines = cards.take(192).joinToString("\n") { card ->
                val name = card.name.map { if (it.code in 32..126) it else '?' }.joinToString("").take(40)
                "${card.id}\t$name\t${if (card.card.phenomenon) 1 else 0}"
            }
            session.raw("POST", "/api/planes", listOf("version" to version, "planes" to lines))
        } catch (e: CancellationException) { throw e } catch (_: Exception) { /* Next connection tries again. */ }
    }

    // --- the table ----------------------------------------------------------------

    /** `pass`, `pause`, `concede`, `win`, `confirm`, `deny`, `starter`, `start`, `cancel-start`, `rematch` or `reset`. */
    override suspend fun counter(seat: TabletSeat, kind: String, value: Int, commander: Int) {
        val key = if (kind == "initiative" || kind == "city-blessing") "value" else "delta"
        val response = post("/api/tablet/counter", seat.fields + listOf("counter" to kind, key to "$value", "commander" to "$commander")) ?: return
        if (response.ok) succeed() else fail(response)
    }

    override suspend fun setEliminationRule(seat: TabletSeat, field: String, value: Int) {
        val response = post("/api/tablet/settings", seat.fields + (field to "$value")) ?: return
        if (response.ok) succeed() else fail(response)
    }

    override suspend fun setAutoEliminateAtZeroLife(seat: TabletSeat, enabled: Boolean) {
        val response = post("/api/tablet/settings", seat.fields + ("autoEliminateAtZeroLife" to if (enabled) "1" else "0")) ?: return
        if (response.ok) succeed() else fail(response)
    }

    override suspend fun control(seat: TabletSeat, action: String): Boolean {
        // Planechase with the players' own planes: "planar:roll:<face>", "planar:effect:<face>",
        // "planar:planeswalk", "planar:chaos" or "planar:counter:<±1>".
        if (action.startsWith(PLANAR_PREFIX)) {
            val parts = action.removePrefix(PLANAR_PREFIX).split(':')
            val fields = when (parts.firstOrNull()) {
                "roll", "effect" -> listOf("action" to "roll", "face" to parts.getOrElse(1) { "" },
                    "fromEffect" to if (parts[0] == "effect") "1" else "0")
                "counter" -> listOf("action" to "counter", "delta" to parts.getOrElse(1) { "" })
                else -> listOf("action" to parts.firstOrNull().orEmpty())
            }
            val response = post("/api/tablet/planar", seat.fields + fields) ?: return false
            if (response.ok) succeed() else fail(response)
            return response.ok
        }
        // Table-wide Magic actions travel as counters on Atlas (protocol/http-v1.md).
        tableCounterFields(action)?.let { fields ->
            val response = post("/api/tablet/counter", seat.fields + fields) ?: return false
            if (response.ok) succeed() else fail(response)
            return response.ok
        }
        val response = post("/api/tablet/control", seat.fields + listOf("action" to action) + (if (action == "start") listOf("askEliminationSource" to "1") else emptyList())) ?: return false
        if (response.ok) succeed() else fail(response)
        return response.ok
    }

    override suspend fun resolveElimination(seat: TabletSeat, victim: Int, source: Int?, token: com.turnhub.android.protocol.GameActionToken): Boolean {
        val response = post("/api/tablet/elimination", seat.fields + listOf("player" to "$victim", "source" to "${source ?: 0}",
            "selfEliminated" to if (source == 0) "1" else "0", "expectedBootId" to token.bootId, "expectedRevision" to "${token.revision}")) ?: return false
        if (response.ok) succeed() else fail(response)
        return response.ok
    }

    override suspend fun doom(seat: TabletSeat, action: com.turnhub.android.protocol.DoomAction,
        token: com.turnhub.android.protocol.GameActionToken, player: Int, value: Int): Boolean {
        val fields = seat.fields + listOf("action" to action.wire, "cycle" to "${token.cycle}",
            "expectedRevision" to "${token.revision}", "expectedBootId" to token.bootId,
            "player" to "$player", "inBag" to "$value", "source" to "$value")
        val response = post("/api/tablet/doom", fields) ?: return false
        if (response.ok) succeed() else fail(response)
        return response.ok
    }

    override suspend fun eliminate(seat: TabletSeat, eliminatedBy: Int?): Boolean {
        val response = post("/api/tablet/control", seat.fields + listOf("action" to "eliminate", "eliminatedBy" to "${eliminatedBy ?: 0}", "selfEliminated" to if (eliminatedBy == 0) "1" else "0")) ?: return false
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

    private val requests = kotlinx.coroutines.sync.Mutex()

    private suspend fun post(path: String, fields: List<Pair<String, String>> = emptyList()): RawResponse? {
        requests.lock()
        _state.update { it.copy(busy = true) }
        val requestId = java.util.UUID.randomUUID().toString()
        var dispatched = false
        try {
            val target = endpoint() ?: throw AtlasException(AtlasFailure.Rejected("Connect to Atlas to use game controls."))
            session.ensureGameAccess(target)
            return try {
                dispatched = true
                session.raw("POST", path, if ((path.startsWith("/api/tablet/") && path !in listOf("/api/tablet/enable", "/api/tablet/disable")) || path == "/api/profiles/register") fields + ("actionId" to requestId) else fields) ?: throw AtlasException(AtlasFailure.Rejected("Atlas reconnected. Try the action again."))
            } catch (e: AtlasException) {
                if (e.failure is AtlasFailure.Timeout || e.failure is AtlasFailure.Unreachable) {
                    val receipt = try { session.raw("GET", "/api/action-receipt", listOf("actionId" to requestId)) }
                        catch (cancel: CancellationException) { throw cancel }
                        catch (_: AtlasException) { null }
                    val result = receipt?.takeIf { it.ok }?.let { json(it) }
                    val status = result?.optInt("status", 0) ?: 0
                    val body = result?.optString("body").orEmpty()
                    val payload = body.takeIf { it.isNotEmpty() }?.let { json(RawResponse(status, it)) }
                    val confirmed = status !in 200..299 || payload?.optBoolean("ok") == true || payload?.optString("status") == "ACCEPTED"
                    if (result?.optBoolean("found") == true && status in 100..599 && payload != null && confirmed)
                        return RawResponse(status, body)
                }
                throw e
            }
        } catch (e: CancellationException) { throw e }
        catch (e: AtlasException) {
            val text = when (e.failure) {
                is AtlasFailure.Timeout, is AtlasFailure.Unreachable -> if (dispatched) "Atlas didn't confirm that. Check the table before trying again." else "Atlas connection is not ready. Try again."
                else -> e.failure.userMessage
            }
            _state.update { it.copy(message = ActionFeedback(text, isError = true)) }
            return null
        } finally {
            _state.update { it.copy(busy = false) }
            requests.unlock()
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

    /**
     * The game menu's day/night and turn order and the player menu's Everyone's counters actions
     * ("everyone:rad:1") as Atlas counter fields; null for other actions.
     */
    private fun tableCounterFields(action: String): List<Pair<String, String>>? = when (action) {
        "day" -> listOf("counter" to "day-night", "value" to "1")
        "night" -> listOf("counter" to "day-night", "value" to "2")
        "day-night-clear" -> listOf("counter" to "day-night", "value" to "0")
        "turn-order-reversed" -> listOf("counter" to "turn-order", "value" to "1")
        "turn-order-clockwise" -> listOf("counter" to "turn-order", "value" to "0")
        else -> if (action.startsWith(com.turnhub.android.standalone.StandaloneGame.EVERYONE_PREFIX)) {
            val parts = action.removePrefix(com.turnhub.android.standalone.StandaloneGame.EVERYONE_PREFIX).split(':')
            if (parts.size == 2) listOf("counter" to "everyone", "kind" to parts[0], "delta" to parts[1]) else null
        } else null
    }
}

/** Per-coroutine presentation context; concurrent seat taps cannot overwrite it. */
class ExpectedGame(val game: Int) : kotlin.coroutines.AbstractCoroutineContextElement(Key) {
    companion object Key : kotlin.coroutines.CoroutineContext.Key<ExpectedGame>
}
