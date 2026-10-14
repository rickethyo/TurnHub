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
class StandaloneViewModel(val table: StandaloneTable) : androidx.lifecycle.ViewModel() {
    /** Survives rotation, but deliberately not process recreation. */
    var entryInitialized = false
}

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
    private val unlockedAccounts = java.util.Collections.synchronizedSet(mutableSetOf<String>())
    fun accountUnlocked(localId: String): Boolean = _state.value.library.players.firstOrNull { it.localId == localId }
        ?.let { it.credential == null || localId in unlockedAccounts } == true
    fun unlockAccount(localId: String, secret: String): Boolean {
        val player = _state.value.library.players.firstOrNull { it.localId == localId } ?: return false
        if (player.credential?.verifies(secret) == false) return false
        unlockedAccounts += localId
        return true
    }
    fun setAccountCredential(localId: String, credential: LocalCredential): Boolean {
        if (!accountUnlocked(localId)) return false
        val before = _state.value
        return publish(before.copy(library = before.library.copy(players = before.library.players.map {
            if (it.localId == localId) it.copy(credential = credential) else it })))
    }

    // --- the lobby ------------------------------------------------------------------

    /** Typing a name creates an identity; selecting a saved player reuses it. */
    fun addPlayer(name: String) {
        val clean = name.trim().take(StandaloneGame.MAX_NAME)
        val before = _state.value
        if (clean.isEmpty() || before.game.state != TableState.LOBBY ||
            before.game.players.size >= StandaloneGame.MAX_PLAYERS) return
        var player = SavedLocalPlayer(newId(), clean)
        while (before.library.players.any { it.profileId == player.profileId } || !LocalProfileIds.valid(player.profileId)) {
            player = player.copy(profileId = LocalProfileIds.create())
        }
        val game = before.game.addPlayer(clean, localId = player.localId)
        publish(before.copy(game = game, library = before.library.copy(players = before.library.players + player)))
    }
    fun selectPlayer(localId: String) {
        val player = _state.value.library.players.firstOrNull { it.localId == localId } ?: return
        edit { it.addPlayer(player.name, localId = player.localId) }
    }
    fun createAccount(name: String, credential: LocalCredential? = null): Boolean {
        val clean = name.trim().take(StandaloneGame.MAX_NAME)
        if (clean.isBlank()) return false
        val before = _state.value
        var player = SavedLocalPlayer(newId(), clean)
        player = player.copy(credential = credential)
        while (before.library.players.any { it.profileId == player.profileId } || !LocalProfileIds.valid(player.profileId)) {
            player = player.copy(profileId = LocalProfileIds.create())
        }
        val saved = publish(before.copy(library = before.library.copy(players = before.library.players + player)))
        if (saved) unlockedAccounts += player.localId
        return saved
    }

    fun saveAccount(localId: String, name: String, personalization: LocalPersonalization): Boolean {
        if (!accountUnlocked(localId)) return false
        val before = _state.value
        if (name.isBlank() || name.length > StandaloneGame.MAX_NAME || before.library.players.none { it.localId == localId }) return false
        if (runCatching { LocalPersonalization.fromJson(personalization.toJson()) }.isFailure) return false
        val library = before.library.copy(players = before.library.players.map {
            if (it.localId == localId) it.copy(name = name.trim(), personalization = personalization) else it })
        // Historical names remain snapshots. Active labels are presentation, so rename immediately.
        val game = before.game.copy(players = before.game.players.map {
            if (it.localId == localId) it.copy(name = name.trim()) else it }, revision = before.game.revision + 1)
        return publish(before.copy(game = game, library = library))
    }

    /** Called only after Atlas creation or credential verification; never from a name match. */
    fun linkAccount(localId: String, atlasId: String, profileId: String): Boolean {
        val before = _state.value
        if (atlasId.isBlank() || !LocalProfileIds.valid(profileId) || before.library.players.none { it.localId == localId } ||
            before.library.players.any { it.localId != localId && it.links.any { l -> l.atlasId == atlasId && l.profileId == profileId } }) return false
        return publish(before.copy(library = before.library.copy(players = before.library.players.map {
            if (it.localId != localId) it else it.copy(links = it.links.filterNot { l -> l.atlasId == atlasId } + AtlasProfileLink(atlasId, profileId)) })))
    }

    fun queueAutomaticImports(atlasId: String, available: Set<String>) {
        _state.value.library.history.forEach { record ->
            if (_state.value.library.delivery(record.recordId).status == DeliveryStatus.NEEDS_LINKING) {
                _state.value.library.automaticMapping(record, atlasId, available)?.let { queueImport(record.recordId, atlasId, it, available) }
            }
        }
    }
    fun removePlayer(index: Int) = edit { it.removePlayer(index) }
    fun clearPlayers() = edit { it.clearPlayers() }
    fun movePlayer(index: Int, by: Int) = edit { it.movePlayer(index, by) }
    fun swapPlayers(a: Int, b: Int) = edit { it.swapPlayers(a, b) }
    fun setFormat(profile: GameProfile) = edit { it.setFormat(profile) }
    fun setStartingLife(life: Int) = edit { it.setStartingLife(life) }
    fun setTurnTimer(ms: Long) = edit { it.setTurnTimer(ms) }
    fun setTwoHeadedGiant(on: Boolean) = edit { it.setTwoHeadedGiant(on) }
    fun setHouseRules(on: Boolean) = edit { it.setHouseRules(on) }
    /** One settings update from the shared lobby, preserving unspecified fields. */
    fun configure(profile: GameProfile?, life: Int?, teams: Boolean?) = edit { game ->
        var next = profile?.let { game.setFormat(it) } ?: game
        teams?.let { next = next.setTwoHeadedGiant(it) }
        life?.let { next = next.setStartingLife(it) }
        next
    }
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

    /** Gives a waiting (not yet acknowledged) match back to linking, e.g. for another Atlas. */
    fun cancelImport(recordId: String): Boolean {
        val before = _state.value
        val old = before.library.delivery(recordId)
        if (old.status != DeliveryStatus.PENDING) return false
        return publish(before.copy(library = before.library.withDelivery(MatchDelivery(recordId))))
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
                "eliminate" -> game.eliminate(index, now)
                "monarch" -> game.claimMonarch(index)
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
        edit { it.changeLife(StandaloneGame.indexOf(seat.moduleId), delta, wallClock()) }
        return _state.value.game !== before
    }

    override suspend fun commander(seat: TabletSeat, source: Int, commander: Int, delta: Int) =
        edit { it.commanderDamage(StandaloneGame.indexOf(seat.moduleId), source - 1, commander, delta, wallClock()) }

    // A standalone game has no phones asking for life changes, and is never offline.
    fun setZeroLifeElimination(enabled: Boolean) { edit { it.setAutoEliminateAtZeroLife(enabled, wallClock()) } }

    override suspend fun counter(seat: TabletSeat, kind: String, value: Int, commander: Int) =
        edit { it.changeCounter(StandaloneGame.indexOf(seat.moduleId), kind, value, commander, wallClock()) }

    override suspend fun setEliminationRule(seat: TabletSeat, field: String, value: Int) =
        edit { it.setEliminationRule(field, value, wallClock()) }

    override suspend fun setAutoEliminateAtZeroLife(seat: TabletSeat, enabled: Boolean) {
        setZeroLifeElimination(enabled)
    }

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
            if (savedLibrary != null && JSONObject(savedLibrary).getInt("schema") == 1) {
                check(store.saveSnapshot(gameToJson(game).toString(), library.toJson().toString())) { "Could not save upgraded accounts" }
            }
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
            put("autoEliminateAtZeroLife", game.autoEliminateAtZeroLife)
            put("active", game.active ?: JSONObject.NULL)
            put("starter", game.starter ?: JSONObject.NULL)
            put("winner", game.winner ?: JSONObject.NULL)
            put("winners", JSONArray(game.winners))
            put("monarch", game.monarch ?: JSONObject.NULL)
            put("initiative", game.initiative ?: JSONObject.NULL)
            put("autoEliminateAtPoisonLimit", game.autoEliminateAtPoisonLimit)
            put("autoEliminateAtCommanderDamage", game.autoEliminateAtCommanderDamage)
            put("poisonLimit", game.poisonLimit)
            put("turnTimerMs", game.turnTimerMs)
            put("twoHeadedGiant", game.twoHeadedGiant)
            put("houseRules", game.houseRules)
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
                        put("conceded", p.conceded)
                        put("counters", JSONObject().apply {
                            put("poison", p.counters.poison); put("energy", p.counters.energy); put("experience", p.counters.experience)
                            put("commanderCasts", JSONArray(p.counters.commanderCasts)); put("cityBlessing", p.counters.cityBlessing)
                        })
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
                autoEliminateAtZeroLife = json.optBoolean("autoEliminateAtZeroLife", true),
                active = json.optIntOrNull("active"),
                starter = json.optIntOrNull("starter"),
                winner = json.optIntOrNull("winner"),
                // Saved before shared victories: the single winner, if any.
                winners = json.optIntList("winners") ?: listOfNotNull(json.optIntOrNull("winner")),
                monarch = json.optIntOrNull("monarch"),
                initiative = json.optIntOrNull("initiative"),
                autoEliminateAtPoisonLimit = json.optBoolean("autoEliminateAtPoisonLimit", true),
                autoEliminateAtCommanderDamage = json.optBoolean("autoEliminateAtCommanderDamage", true),
                poisonLimit = json.optInt("poisonLimit", 10),
                turnTimerMs = json.optLong("turnTimerMs", 0),
                twoHeadedGiant = json.optBoolean("twoHeadedGiant"),
                houseRules = json.optBoolean("houseRules"),
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
                        conceded = p.optBoolean("conceded"),
                        counters = p.optJSONObject("counters")?.let { c ->
                            com.turnhub.android.protocol.PlayerCounters(c.getInt("poison"), c.getInt("energy"), c.getInt("experience"),
                                c.getJSONArray("commanderCasts").let { a -> List(2) { a.getInt(it) } }, c.getBoolean("cityBlessing"))
                        } ?: com.turnhub.android.protocol.PlayerCounters(),
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
