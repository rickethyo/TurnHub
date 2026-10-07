package com.turnhub.android.standalone

import android.content.Context
import com.turnhub.android.data.OfflineChange
import com.turnhub.android.data.TableControls
import com.turnhub.android.data.TabletSeat
import com.turnhub.android.protocol.GameProfile
import com.turnhub.android.protocol.ProfileSummary
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

/** Where the standalone game, its finished records and the cached Atlas profiles are kept. */
interface StandaloneStore {
    fun loadGame(): String?
    fun saveGame(json: String)
    fun loadRecords(): String?
    fun saveRecords(json: String)
    fun loadProfiles(): String?
    fun saveProfiles(json: String)
}

class PreferencesStandaloneStore(context: Context) : StandaloneStore {
    private val prefs = context.applicationContext.getSharedPreferences("turnhub_standalone", Context.MODE_PRIVATE)
    override fun loadGame(): String? = prefs.getString(GAME, null)
    override fun saveGame(json: String) = prefs.edit().putString(GAME, json).apply()
    override fun loadRecords(): String? = prefs.getString(RECORDS, null)
    override fun saveRecords(json: String) = prefs.edit().putString(RECORDS, json).apply()
    override fun loadProfiles(): String? = prefs.getString(PROFILES, null)
    override fun saveProfiles(json: String) = prefs.edit().putString(PROFILES, json).apply()

    private companion object {
        const val GAME = "game"
        const val RECORDS = "records"
        const val PROFILES = "profiles"
    }
}

/** A profile from the last Atlas this device was connected to, for picking players offline. */
data class KnownProfile(val profileId: String, val name: String)

data class StandaloneState(
    val game: StandaloneGame = StandaloneGame(),
    /** Finished games waiting for an Atlas to import them, oldest first. */
    val records: List<GameRecord> = emptyList(),
    val knownProfiles: List<KnownProfile> = emptyList(),
)

/**
 * Runs the standalone tablet game: the tablet table screen's [TableControls]
 * apply to [StandaloneGame] here instead of going to Atlas. Every change is
 * saved, so closing the app keeps the game; each finished game is kept as a
 * [GameRecord] for Atlas.
 */
class StandaloneTable(
    private val store: StandaloneStore,
    private val wallClock: () -> Long = System::currentTimeMillis,
    private val newId: () -> String = { UUID.randomUUID().toString() },
) : TableControls {
    private val _state = MutableStateFlow(load())
    val state: StateFlow<StandaloneState> = _state.asStateFlow()

    // --- the lobby ------------------------------------------------------------------

    fun addPlayer(name: String, profileId: String? = null) = edit { it.addPlayer(name, profileId) }
    fun removePlayer(index: Int) = edit { it.removePlayer(index) }
    fun movePlayer(index: Int, by: Int) = edit { it.movePlayer(index, by) }
    fun setFormat(profile: GameProfile) = edit { it.setFormat(profile) }
    fun setStartingLife(life: Int) = edit { it.setStartingLife(life) }
    fun start() = edit { it.start(wallClock(), newId()) }

    /** Remembers the profiles of the Atlas just connected to, for the lobby's quick picks. */
    fun rememberProfiles(profiles: List<ProfileSummary>) {
        val known = profiles.map { KnownProfile(it.profileId, it.name) }.filter { it.name.isNotBlank() }
        if (known == _state.value.knownProfiles) return
        _state.update { it.copy(knownProfiles = known) }
        store.saveProfiles(JSONArray().apply {
            known.forEach { put(JSONObject().put("profileId", it.profileId).put("name", it.name)) }
        }.toString())
    }

    /** Drops records Atlas has taken (imported, or already had). */
    fun forgetRecords(ids: Set<String>) {
        if (ids.isEmpty()) return
        _state.update { it.copy(records = it.records.filterNot { r -> r.recordId in ids }) }
        saveRecords()
    }

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
        _state.update { s -> s.copy(game = after, records = if (record != null) (s.records + record).takeLast(MAX_RECORDS) else s.records) }
        store.saveGame(gameToJson(after).toString())
        if (record != null) saveRecords()
    }

    private fun saveRecords() =
        store.saveRecords(JSONArray().apply { _state.value.records.forEach { put(it.toJson()) } }.toString())

    private fun load(): StandaloneState {
        val game = store.loadGame()?.let { runCatching { gameFromJson(JSONObject(it)) }.getOrNull() } ?: StandaloneGame()
        val records = store.loadRecords()?.let { text ->
            runCatching {
                val array = JSONArray(text)
                (0 until array.length()).mapNotNull { GameRecord.fromJson(array.getJSONObject(it)) }
            }.getOrNull()
        }.orEmpty()
        val profiles = store.loadProfiles()?.let { text ->
            runCatching {
                val array = JSONArray(text)
                (0 until array.length()).map { i ->
                    array.getJSONObject(i).let { KnownProfile(it.getString("profileId"), it.getString("name")) }
                }
            }.getOrNull()
        }.orEmpty()
        return StandaloneState(game, records, profiles)
    }

    companion object {
        /** Finished games kept for Atlas; the oldest go first past this. */
        const val MAX_RECORDS = 200

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
                        put("life", p.life)
                        put("eliminated", p.eliminated)
                        put("outOrder", p.outOrder ?: JSONObject.NULL)
                        put("turnsCompleted", p.turnsCompleted)
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
                state = TableState.fromWire(json.getString("state")) ?: TableState.LOBBY,
                profile = GameProfile.fromWire(json.getString("gameProfile")) ?: GameProfile.MTG_COMMANDER,
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
                        profileId = p.optStringOrNull("profileId"),
                        life = p.getInt("life"),
                        eliminated = p.getBoolean("eliminated"),
                        outOrder = p.optIntOrNull("outOrder"),
                        turnsCompleted = p.getInt("turnsCompleted"),
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
