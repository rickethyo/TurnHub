package com.turnhub.android.standalone

import android.content.Context
import com.turnhub.android.data.RawResponse
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import com.turnhub.android.data.OfflineChange
import com.turnhub.android.data.TableControls
import com.turnhub.android.data.TabletSeat
import com.turnhub.android.protocol.GameProfile
import com.turnhub.android.protocol.TableState
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import org.json.JSONArray
import org.json.JSONObject
import java.util.UUID

/** Owns the [StandaloneTable] for the Activity, so rotation keeps it. */
class StandaloneViewModel(val table: StandaloneTable) : androidx.lifecycle.ViewModel()

/** Android owns the active game and a separate, versioned local library. */
interface StandaloneStore {
    fun loadGame(): String?
    /** Read-only legacy import queue, retained without inferred identities. */
    fun loadRecords(): String?
    fun loadLibrary(): String?
    /** Production stores both keys in one preference transaction. */
    fun saveSnapshot(game: String, library: String): Boolean
    /** Copies unreadable saved data aside before a fresh start; false if it could not be kept. */
    fun keepUnreadable(): Boolean = false
}

class PreferencesStandaloneStore(context: Context) : StandaloneStore {
    private val prefs = context.applicationContext.getSharedPreferences("turnhub_standalone", Context.MODE_PRIVATE)
    override fun loadGame(): String? = prefs.getString("game", null)
    override fun loadRecords(): String? = prefs.getString("records", null)
    override fun loadLibrary(): String? = prefs.getString("library", null)
    override fun saveSnapshot(game: String, library: String): Boolean = prefs.edit()
        .putString("game", game).putString("library", library).apply().let { true }

    // Both keys go in one editor, so the swap is atomic; the old data stays under *_unreadable.
    override fun keepUnreadable(): Boolean = prefs.edit()
        .putString("game_unreadable", prefs.getString("game", null))
        .putString("library_unreadable", prefs.getString("library", null))
        .putString("records_unreadable", prefs.getString("records", null))
        .commit()
}

data class StandaloneState(
    val game: StandaloneGame = StandaloneGame(),
    val library: LocalLibrary = LocalLibrary(),
    /** A damaged/unsupported library is kept untouched rather than silently reset. */
    val storageProblem: String? = null,
)

/**
 * Runs the standalone tablet game: the tablet table screen's [TableControls]
 * apply to [StandaloneGame] here instead of going to Atlas. Every change is
 * saved, so closing the app keeps the game; each finished game is kept as a
 * immutable [GameRecord] in local history, independent of optional Atlas delivery.
 */
class StandaloneTable(
    private val store: StandaloneStore,
    private val wallClock: () -> Long = System::currentTimeMillis,
    private val newId: () -> String = { UUID.randomUUID().toString() },
) : TableControls {
    private val _state = MutableStateFlow(load())
    val state: StateFlow<StandaloneState> = _state.asStateFlow()

    private val deliveryMutex = Mutex()

    // --- the lobby ------------------------------------------------------------------

    /** Typing a name creates an identity; selecting a saved player reuses it. */
    fun addPlayer(name: String) {
        val clean = name.trim().take(StandaloneGame.MAX_NAME)
        val before = _state.value
        if (clean.isEmpty() || before.game.state != TableState.LOBBY ||
            before.game.players.size >= StandaloneGame.MAX_PLAYERS) return
        val player = SavedLocalPlayer(newId(), clean)
        val game = before.game.addPlayer(clean, localId = player.localId)
        publish(before.copy(game = game, library = before.library.copy(players = before.library.players + player)))
    }
    fun selectPlayer(localId: String) {
        val player = _state.value.library.players.firstOrNull { it.localId == localId } ?: return
        edit { it.addPlayer(player.name, localId = player.localId) }
    }
    fun removePlayer(index: Int) = edit { it.removePlayer(index) }
    fun movePlayer(index: Int, by: Int) = edit { it.movePlayer(index, by) }
    fun setFormat(profile: GameProfile) = edit { it.setFormat(profile) }
    fun setStartingLife(life: Int) = edit { it.setStartingLife(life) }
    fun start() = edit { it.start(wallClock(), newId()) }

    /** A pending/acknowledged payload cannot be silently redirected or remapped. */
    fun queueImport(recordId: String, atlasId: String, profileIds: List<String>, availableProfileIds: Set<String>): Boolean {
        val before = _state.value
        val record = before.library.history.firstOrNull { it.recordId == recordId } ?: return false
        if (atlasId.isBlank() || profileIds.size != record.players.size ||
            profileIds.any { it.isBlank() || it !in availableProfileIds } ||
            profileIds.distinct().size != profileIds.size) return false
        val old = before.library.delivery(recordId)
        if (old.status == DeliveryStatus.IMPORTED) return false
        if (old.status == DeliveryStatus.PENDING) return old.atlasId == atlasId && old.profileIds == profileIds
        return publish(before.copy(library = before.library.withDelivery(
            MatchDelivery(recordId, DeliveryStatus.PENDING, atlasId = atlasId, profileIds = profileIds.toList()))))
    }

    /** Sends only explicitly mapped work for this Atlas, serialized and oldest first. */
    suspend fun sendRecords(atlasId: String, post: suspend (List<Pair<String, String>>) -> RawResponse?): Int =
        deliveryMutex.withLock {
            var taken = 0
            for (record in _state.value.library.history) {
                val delivery = _state.value.library.delivery(record.recordId)
                if (delivery.status != DeliveryStatus.PENDING || delivery.atlasId != atlasId) continue
                if (_state.value.storageProblem != null) break
                val response = post(record.importFields(atlasId, delivery.profileIds))
                val next = when {
                    response == null -> delivery.copy(reason = "No acknowledgement from Atlas. Retry this same mapping after reconnecting.")
                    response.code == 400 -> delivery.copy(status = DeliveryStatus.REJECTED,
                        reason = response.reason("Atlas rejected this record (HTTP 400). Review the player mapping."))
                    response.ok -> {
                        val detail = runCatching {
                            val ack = JSONObject(response.body)
                            require(ack.getBoolean("ok"))
                            val duplicate = ack.getBoolean("duplicate")
                            val credited = ack.getInt("credited")
                            require(credited in 0..record.players.size)
                            val unmatched = ack.getJSONArray("unmatched")
                            val names = (0 until unmatched.length()).map { unmatched.getString(it) }
                            if (duplicate) "Atlas already had this match."
                            else "Atlas credited $credited of ${record.players.size} players." +
                                if (names.isEmpty()) "" else " Not credited: ${names.joinToString()}."
                        }.getOrNull()
                        if (detail != null) delivery.copy(status = DeliveryStatus.IMPORTED, reason = detail)
                        else delivery.copy(reason = "Atlas returned no valid import acknowledgement. Retry the same mapping.")
                    }
                    else -> delivery.copy(reason = response.reason("Atlas import is waiting (HTTP ${response.code}). Retry after reconnecting."))
                }
                if (!publish(_state.value.copy(library = _state.value.library.withDelivery(next)))) break
                if (next.status == DeliveryStatus.IMPORTED) taken++
                // Rejected records stay visible but do not block other queued work.
                // A lost acknowledgement blocks newer work so retries stay inside
                // Atlas's bounded receipt window rather than evicting uncertain IDs.
                if (next.status == DeliveryStatus.PENDING) break
            }
            taken
        }

    private fun RawResponse.reason(fallback: String): String = runCatching {
        JSONObject(body).optString("error").takeIf { it.isNotBlank() }?.take(300)
    }.getOrNull() ?: fallback

    // --- the table ------------------------------------------------------------------

    override suspend fun control(seat: TabletSeat, action: String): Boolean {
        val index = StandaloneGame.indexOf(seat.moduleId)
        val now = wallClock()
        val before = _state.value.game
        edit { game ->
            when (action) {
                "pass" -> game.pass(index, now)
                "pause" -> game.togglePause(now)
                "concede" -> game.concede(index, now)
                "win" -> game.claimWin(index, now)
                "draw" -> game.endWithoutWinner(now)
                "rematch" -> game.rematch(now, newId())
                "reset" -> game.reset()
                else -> game
            }
        }
        return _state.value.game !== before
    }

    override suspend fun life(seat: TabletSeat, delta: Int): Boolean {
        val before = _state.value.game
        edit { it.changeLife(StandaloneGame.indexOf(seat.moduleId), delta) }
        return _state.value.game !== before
    }

    override suspend fun commander(seat: TabletSeat, source: Int, commander: Int, delta: Int) =
        edit { it.commanderDamage(StandaloneGame.indexOf(seat.moduleId), source - 1, commander, delta) }

    // A standalone game has no phones asking for life changes, and is never offline.
    override suspend fun respondLife(seat: TabletSeat, requestId: Long, accept: Boolean) = Unit
    override fun queueOffline(change: OfflineChange) = Unit
    override fun refuseOffline() = Unit
    override suspend fun disable() = Unit

    // --- plumbing -------------------------------------------------------------------

    private fun edit(change: (StandaloneGame) -> StandaloneGame) {
        val before = _state.value.game
        val after = change(before)
        if (after === before) return
        val record = after.record()?.takeIf { before.state != TableState.GAME_OVER || before.gameId != after.gameId }
        val library = record?.let { _state.value.library.withRecord(it) } ?: _state.value.library
        publish(_state.value.copy(game = after, library = library))
    }

    /** Leaves a blocked store: keeps the unreadable data aside, then starts empty. */
    fun startFresh(): Boolean {
        if (_state.value.storageProblem == null || !store.keepUnreadable()) return false
        val fresh = StandaloneState()
        val saved = runCatching { store.saveSnapshot(gameToJson(fresh.game).toString(), fresh.library.toJson().toString()) }
            .getOrDefault(false)
        if (saved) _state.value = fresh
        return saved
    }

    private fun publish(next: StandaloneState): Boolean {
        if (_state.value.storageProblem != null) return false
        val saved = runCatching { store.saveSnapshot(gameToJson(next.game).toString(), next.library.toJson().toString()) }
            .getOrDefault(false)
        if (!saved) {
            _state.update { it.copy(storageProblem = "Couldn't save local play. Close and reopen the app before continuing; do not clear app data.") }
            return false
        }
        _state.value = next
        return true
    }

    private fun load(): StandaloneState {
        return try {
            val game = store.loadGame()?.let { gameFromJson(JSONObject(it)) } ?: StandaloneGame()
            val savedLibrary = store.loadLibrary()
            var library = if (savedLibrary != null) LocalLibrary.fromJson(JSONObject(savedLibrary)) else {
                // Preserve the old queue as history, without inventing identity from its labels.
                val records = store.loadRecords()?.let { JSONArray(it) } ?: JSONArray()
                LocalLibrary(history = (0 until records.length()).map { i ->
                    requireNotNull(GameRecord.fromJson(records.getJSONObject(i)))
                })
            }
            // Also covers a game that was completed before its old queue write reached storage.
            game.record()?.let { library = library.withRecord(it) }
            StandaloneState(game = game, library = library)
        } catch (_: Exception) {
            StandaloneState(storageProblem = "Local saved data could not be read. It has been kept untouched. Do not clear app data; recovery needs review.")
        }
    }

    companion object {
        fun gameToJson(game: StandaloneGame): JSONObject = JSONObject().apply {
            put("gameId", game.gameId)
            put("state", game.state.name)
            put("gameProfile", game.profile.wireValue)
            put("startingLife", game.startingLife)
            put("active", game.active ?: JSONObject.NULL)
            put("starter", game.starter ?: JSONObject.NULL)
            put("winner", game.winner ?: JSONObject.NULL)
            put("startedAtMs", game.startedAtMs)
            put("elapsedMs", game.elapsedMs)
            put("runningSinceMs", game.runningSinceMs ?: JSONObject.NULL)
            put("turnStartedAtElapsedMs", game.turnStartedAtElapsedMs)
            put("revision", game.revision)
            put("players", JSONArray().apply {
                game.players.forEach { p ->
                    put(JSONObject().apply {
                        put("name", p.name)
                        put("profileId", p.profileId ?: JSONObject.NULL)
                        put("localId", p.localId ?: JSONObject.NULL)
                        put("life", p.life)
                        put("eliminated", p.eliminated)
                        put("outOrder", p.outOrder ?: JSONObject.NULL)
                        put("turnsCompleted", p.turnsCompleted)
                        put("turnMs", p.turnMs)
                        put("fastestTurnMs", p.fastestTurnMs)
                        put("longestTurnMs", p.longestTurnMs)
                        put("commanderDamage", JSONObject().apply {
                            p.commanderDamage.forEach { (source, damage) -> put("$source", JSONArray(damage)) }
                        })
                    })
                }
            })
        }

        fun gameFromJson(json: JSONObject): StandaloneGame {
            val players = json.getJSONArray("players")
            return StandaloneGame(
                gameId = json.getString("gameId"),
                state = TableState.fromWire(json.getString("state")) ?: error("Unsupported local game state"),
                profile = GameProfile.fromWire(json.getString("gameProfile")) ?: error("Unsupported local game format"),
                startingLife = json.getInt("startingLife"),
                active = json.optIntOrNull("active"),
                starter = json.optIntOrNull("starter"),
                winner = json.optIntOrNull("winner"),
                startedAtMs = json.getLong("startedAtMs"),
                elapsedMs = json.getLong("elapsedMs"),
                runningSinceMs = json.optLongOrNull("runningSinceMs"),
                turnStartedAtElapsedMs = json.getLong("turnStartedAtElapsedMs"),
                revision = json.getLong("revision"),
                players = (0 until players.length()).map { i ->
                    val p = players.getJSONObject(i)
                    val damage = p.getJSONObject("commanderDamage")
                    LocalPlayer(
                        name = p.getString("name"),
                        localId = p.optStringOrNull("localId"),
                        profileId = p.optStringOrNull("profileId"),
                        life = p.getInt("life"),
                        eliminated = p.getBoolean("eliminated"),
                        outOrder = p.optIntOrNull("outOrder"),
                        turnsCompleted = p.getInt("turnsCompleted"),
                        turnMs = p.optLong("turnMs"),
                        fastestTurnMs = p.optLong("fastestTurnMs"),
                        longestTurnMs = p.optLong("longestTurnMs"),
                        commanderDamage = damage.keys().asSequence().associate { key ->
                            val array = damage.getJSONArray(key)
                            key.toInt() to (0 until array.length()).map { array.getInt(it) }
                        },
                    )
                },
            )
        }
    }
}
