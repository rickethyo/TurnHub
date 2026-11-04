package com.turnhub.android.standalone

import org.json.JSONObject
import kotlin.random.Random

/** Stateless bridge to the same C++ rules linked by Atlas. No network or host promotion. */
internal object NativeGameCore {
    init {
        val hostLibrary = System.getProperty("turnhub.core.library")
        if (hostLibrary != null) System.load(hostLibrary) else System.loadLibrary("turnhub_game")
    }

    private external fun dispatch(state: ByteArray, action: ByteArray, random: Random): ByteArray

    fun apply(state: JSONObject, action: JSONObject, random: Random = Random.Default): JSONObject =
        JSONObject(dispatch(state.toString().toByteArray(Charsets.UTF_8),
            action.toString().toByteArray(Charsets.UTF_8), random).toString(Charsets.UTF_8))

    /**
     * Offline play: while Atlas is out of reach, every action applied to the
     * game [OfflineRecorder.gameId] names is handed to the recorder with the
     * random draws it took, so Atlas can replay it later. Turn timeouts are
     * left out: Atlas works them out again from the clock.
     */

    fun apply(game: StandaloneGame, action: JSONObject, random: Random = Random.Default): StandaloneGame {
        if (game.offlineRecorder?.recording == false) return game
        val listening = game.offlineRecorder?.takeIf { it.recording && it.gameId == game.gameId && action.optString("do") !in UNRECORDED }
        val draws = if (listening != null) RecordingRandom(random) else null
        val saved = StandaloneTable.gameToJson(game)
        val result = apply(JSONObject(saved.toString()).put("profile", game.profile.name), action, draws ?: random)
        if (result.getLong("revision") == game.revision) return game
        val next = fromCore(result, saved).copy(offlineRecorder = game.offlineRecorder)
        if (listening?.record(JSONObject(action.toString()), draws?.draws.orEmpty(), next) == false) return game
        return next
    }

    /** A game from the core's state JSON ([base] supplies what only the app keeps). */
    fun fromCore(state: JSONObject, base: JSONObject = StandaloneTable.gameToJson(StandaloneGame())): StandaloneGame {
        state.keys().forEach { key -> base.put(key, state.get(key)) }
        base.put("gameProfile", com.turnhub.android.protocol.GameProfile.valueOf(state.getString("profile")).wireValue)
        return StandaloneTable.gameFromJson(base)
    }

    private val UNRECORDED = setOf("applyTurnTimeout", "record")

    fun record(game: StandaloneGame): GameRecord? {
        val state = StandaloneTable.gameToJson(game).put("profile", game.profile.name)
        val text = dispatch(state.toString().toByteArray(Charsets.UTF_8),
            "{\"do\":\"record\"}".toByteArray(), Random.Default).toString(Charsets.UTF_8)
        return if (text == "null") null else requireNotNull(GameRecord.fromJson(JSONObject(text)))
    }
}

/** Receives each action applied to one game while Atlas is offline (bound to that game instance). */
interface OfflineRecorder {
    val gameId: String
    val recording: Boolean
    fun record(action: JSONObject, draws: List<Int>, next: StandaloneGame): Boolean
}

/** Passes draws through and keeps them, as the core's Random sees them (a coin is a draw of 2). */
internal class RecordingRandom(private val inner: Random) : Random() {
    val draws = mutableListOf<Int>()
    override fun nextBits(bitCount: Int): Int = inner.nextBits(bitCount)
    override fun nextInt(until: Int): Int = inner.nextInt(until).also { draws += it }
    override fun nextBoolean(): Boolean = inner.nextBoolean().also { draws += if (it) 1 else 0 }
}

/** Every format runs on the shared core; tests can replay the Kotlin oracle. */
internal object GameRuleRuntime {
    private val oracle = ThreadLocal.withInitial { java.lang.Boolean.getBoolean("turnhub.core.reference") }
    fun shared(game: StandaloneGame): Boolean = oracle.get() != true
    fun <T> reference(block: () -> T): T {
        val before = oracle.get()
        oracle.set(true)
        return try { block() } finally { oracle.set(before) }
    }
}
