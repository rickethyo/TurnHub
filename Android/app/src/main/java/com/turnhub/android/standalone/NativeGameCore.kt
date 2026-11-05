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

    fun apply(game: StandaloneGame, action: JSONObject, random: Random = Random.Default): StandaloneGame {
        val saved = StandaloneTable.gameToJson(game)
        val result = apply(JSONObject(saved.toString()).put("profile", game.profile.name), action, random)
        if (result.getLong("revision") == game.revision) return game
        return fromCore(result, saved)
    }

    /** A game from the core's state JSON ([base] supplies what only the app keeps). */
    fun fromCore(state: JSONObject, base: JSONObject = StandaloneTable.gameToJson(StandaloneGame())): StandaloneGame {
        state.keys().forEach { key -> base.put(key, state.get(key)) }
        base.put("gameProfile", com.turnhub.android.protocol.GameProfile.valueOf(state.getString("profile")).wireValue)
        return StandaloneTable.gameFromJson(base)
    }


    fun record(game: StandaloneGame): GameRecord? {
        val state = StandaloneTable.gameToJson(game).put("profile", game.profile.name)
        val text = dispatch(state.toString().toByteArray(Charsets.UTF_8),
            "{\"do\":\"record\"}".toByteArray(), Random.Default).toString(Charsets.UTF_8)
        return if (text == "null") null else requireNotNull(GameRecord.fromJson(JSONObject(text)))
    }
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
