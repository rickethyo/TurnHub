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
        result.keys().forEach { key -> saved.put(key, result.get(key)) }
        saved.put("gameProfile", com.turnhub.android.protocol.GameProfile.valueOf(result.getString("profile")).wireValue)
        return StandaloneTable.gameFromJson(saved)
    }

    fun record(game: StandaloneGame): GameRecord? {
        val state = StandaloneTable.gameToJson(game).put("profile", game.profile.name)
        val text = dispatch(state.toString().toByteArray(Charsets.UTF_8),
            "{\"do\":\"record\"}".toByteArray(), Random.Default).toString(Charsets.UTF_8)
        return if (text == "null") null else requireNotNull(GameRecord.fromJson(JSONObject(text)))
    }
}

/** Local-only licensed-module rules remain separate; tests can replay the Kotlin oracle. */
internal object GameRuleRuntime {
    private val oracle = ThreadLocal.withInitial { java.lang.Boolean.getBoolean("turnhub.core.reference") }
    fun shared(game: StandaloneGame): Boolean = !game.profile.deathMayDie && oracle.get() != true
    fun <T> reference(block: () -> T): T {
        val before = oracle.get()
        oracle.set(true)
        return try { block() } finally { oracle.set(before) }
    }
}
