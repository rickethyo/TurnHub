package com.turnhub.android.data

import com.turnhub.android.protocol.TableState
import com.turnhub.android.standalone.LocalLibrary
import com.turnhub.android.standalone.MemoryStandaloneStore
import com.turnhub.android.standalone.NativeGameCore
import com.turnhub.android.standalone.OfflineRecorder
import com.turnhub.android.standalone.StandaloneTable
import org.json.JSONArray
import org.json.JSONException
import org.json.JSONObject

/**
 * Atlas's whole game as `GET /api/v1/game` gave it (protocol/http-v1.md,
 * "Offline play"): what this phone keeps playing on if Atlas drops off.
 * [fetchedAtMs] is this phone's wall clock when it arrived, so
 * [clockOffset] turns the phone's clock into Atlas's game clock.
 */
data class OfflineBase(
    val atlasId: String,
    val bootId: String,
    val game: Int,
    val atlasMs: Long,
    val fetchedAtMs: Long,
    val state: JSONObject,
) {
    val revision: Long get() = state.optLong("revision")
    val gameId: String get() = state.optString("gameId")
    val playing: Boolean get() = state.optString("state") in setOf(TableState.RUNNING.name, TableState.PAUSED.name)
    /** Phone clock minus Atlas's game clock. */
    val clockOffset: Long get() = fetchedAtMs - atlasMs

    fun matches(summary: com.turnhub.android.domain.TableSummary): Boolean =
        atlasId == summary.atlasId && game == summary.game && gameId == summary.matchId

    companion object {
        fun parse(body: String, nowMs: Long): OfflineBase? = try {
            val json = JSONObject(body)
            val state = json.getJSONObject("state")
            if (!state.has("gameId") || !state.has("profile")) null
            else OfflineBase(json.getString("atlasId"), json.getString("bootId"), json.optInt("game", 1),
                json.getLong("atlasMs"), nowMs, state)
        } catch (_: JSONException) {
            null
        }
    }
}

/** One action this phone applied while Atlas was out of reach, on Atlas's game clock. */
data class OfflineEntry(val seq: Long, val atMs: Long, val action: JSONObject, val draws: List<Int>) {
    fun toJson(): JSONObject = JSONObject().put("seq", seq).put("atMs", atMs).put("action", action)
        .put("draws", JSONArray(draws))
}

/**
 * The game going on on this phone while Atlas is out of reach (owner,
 * 2026-11-03). It starts from [base], moved onto this phone's clock, and runs
 * on the same rules as Atlas through an in-memory [StandaloneTable], so the
 * whole table stays playable: passes, life, counters, eliminations, wins.
 * Every rules action is queued with its time on Atlas's clock (the last Atlas
 * time this phone knew plus the time since) and the random draws it took;
 * [syncFields] sends the queue to `POST /api/v1/offline`, where Atlas merges
 * it with other phones' queues in time order and replays it.
 */
class OfflineAtlasGame(
    val base: OfflineBase,
    private val wallClock: () -> Long = System::currentTimeMillis,
    /** This phone's queue name on Atlas; one per outage is enough. */
    val device: String = java.util.UUID.randomUUID().toString().take(12),
    private val persist: (String?) -> Boolean = { true },
    private val restored: JSONObject? = null,
) : OfflineRecorder {
    override val gameId: String = base.gameId
    private val entries = mutableListOf<OfflineEntry>()
    override var recording = false
        private set
    private val _problem = kotlinx.coroutines.flow.MutableStateFlow<String?>(null)
    val problem: kotlinx.coroutines.flow.StateFlow<String?> = _problem

    init {
        restored?.getJSONArray("entries")?.let { saved ->
            require(saved.length() <= 512)
            repeat(saved.length()) { i -> val e = saved.getJSONObject(i)
                val draws = e.optJSONArray("draws") ?: JSONArray()
                entries += OfflineEntry(e.getLong("seq"), e.getLong("atMs"), e.getJSONObject("action"),
                    (0 until draws.length()).map { draws.getInt(it) })
            }
        }
    }

    val table: StandaloneTable = run {
        val atlas = NativeGameCore.fromCore(JSONObject(base.state.toString()))
        val offset = base.clockOffset
        val here = atlas.copy(startedAtMs = atlas.startedAtMs + offset, runningSinceMs = atlas.runningSinceMs?.let { it + offset })
        val current = restored?.getJSONObject("current")?.let { StandaloneTable.gameFromJson(it) } ?: here
        require(current.gameId == gameId)
        StandaloneTable(MemoryStandaloneStore(current, LocalLibrary()), wallClock = wallClock, offlineRecorder = this)
    }

    val queue: List<OfflineEntry> get() = synchronized(entries) { entries.toList() }

    override fun record(action: JSONObject, draws: List<Int>, next: com.turnhub.android.standalone.StandaloneGame): Boolean = synchronized(entries) {
        if (next.gameId != gameId || action.optString("do") in setOf("start", "rematch", "seanModeRematch", "reset", "backToSetup", "addPlayer", "removePlayer", "clearPlayers")) {
            _problem.value = "Reconnect Atlas before changing the match or players. Offline moves remain saved."
            return@synchronized false
        }
        val nowMs = if (action.has("nowMs")) action.getLong("nowMs") else wallClock()
        val entry = OfflineEntry(entries.size + 1L, nowMs - base.clockOffset, JSONObject(action.toString()).apply { remove("nowMs") }, draws)
        val pending = entries + entry
        if (pending.size > 512 || JSONArray(pending.map { it.toJson() }).toString().length > 65536) {
            _problem.value = "The offline queue is full. Reconnect Atlas before making more edits."
            return@synchronized false
        }
        if (!runCatching { persist(saved(next, pending).toString()) }.getOrDefault(false)) {
            _problem.value = "Couldn't save offline play. This edit was not applied; free storage and try again."
            return@synchronized false
        }
        entries += entry
        _problem.value = null
        true
    }
    private fun saved(game: com.turnhub.android.standalone.StandaloneGame, pending: List<OfflineEntry>) = JSONObject()
        .put("base", JSONObject().put("atlasId", base.atlasId).put("bootId", base.bootId).put("game", base.game)
            .put("atlasMs", base.atlasMs).put("fetchedAtMs", base.fetchedAtMs).put("state", base.state))
        .put("device", device).put("entries", JSONArray(pending.map { it.toJson() }))
        .put("current", StandaloneTable.gameToJson(game))

    fun save(): Boolean = persist(saved(table.state.value.game, queue).toString())
    fun acknowledge(): Boolean = persist(null)

    companion object {
        fun restore(record: String, persist: (String?) -> Boolean, wallClock: () -> Long = System::currentTimeMillis): OfflineAtlasGame {
            val saved = JSONObject(record)
            val b = saved.getJSONObject("base")
            val base = OfflineBase(b.getString("atlasId"), b.getString("bootId"), b.getInt("game"), b.getLong("atlasMs"),
                b.getLong("fetchedAtMs"), b.getJSONObject("state"))
            return OfflineAtlasGame(base, wallClock, saved.getString("device"), persist, saved)
        }
    }

    /** The `POST /api/v1/offline` form for the queue so far. */
    fun syncFields(): List<Pair<String, String>> = listOf(
        "device" to device,
        "baseBoot" to base.bootId,
        "base" to base.state.toString(),
        "nowMs" to "${wallClock() - base.clockOffset}",
        "entries" to JSONArray(queue.map { it.toJson() }).toString(),
    )

    /** Starts queueing this game's actions. */
    fun listen() { recording = true }

    /** Stops queueing (Atlas has the queue, or the outage is over). */
    fun stop() { recording = false }
}
