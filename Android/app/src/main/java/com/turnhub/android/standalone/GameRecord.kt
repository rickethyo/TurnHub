package com.turnhub.android.standalone

import com.turnhub.android.protocol.GameProfile
import org.json.JSONArray
import org.json.JSONObject

/**
 * A finished standalone game, kept on the tablet until an Atlas imports it.
 * Atlas credits each player that matches one of its profiles ([profileId], or
 * the name when none was picked) and ignores a [recordId] it has seen before.
 * Player indices ([starter], [winner]) are positions in [players], which is
 * turn order.
 */
data class GameRecord(
    val recordId: String,
    val profile: GameProfile,
    val startingLife: Int,
    /** Wall clock (Unix ms) the game started. */
    val startedAtMs: Long,
    val durationMs: Long,
    val starter: Int?,
    /** Null when the game ended with no winner. */
    val winner: Int?,
    val players: List<Player>,
) {
    data class Player(
        val name: String,
        val profileId: String?,
        val finalLife: Int,
        val turnsCompleted: Int,
        /** 1 for the first player out; null for whoever was still in at the end. */
        val outOrder: Int?,
        val commanderDamageReceived: Int,
    )

    fun toJson(): JSONObject = JSONObject().apply {
        put("recordId", recordId)
        put("gameProfile", profile.wireValue)
        put("startingLife", startingLife)
        put("startedAtMs", startedAtMs)
        put("durationMs", durationMs)
        put("starter", starter ?: JSONObject.NULL)
        put("winner", winner ?: JSONObject.NULL)
        put("players", JSONArray().apply {
            players.forEach { p ->
                put(JSONObject().apply {
                    put("name", p.name)
                    put("profileId", p.profileId ?: JSONObject.NULL)
                    put("finalLife", p.finalLife)
                    put("turnsCompleted", p.turnsCompleted)
                    put("outOrder", p.outOrder ?: JSONObject.NULL)
                    put("commanderDamageReceived", p.commanderDamageReceived)
                })
            }
        })
    }

    companion object {
        fun fromJson(json: JSONObject): GameRecord? = runCatching {
            val players = json.getJSONArray("players")
            GameRecord(
                recordId = json.getString("recordId"),
                profile = GameProfile.fromWire(json.getString("gameProfile")) ?: return null,
                startingLife = json.getInt("startingLife"),
                startedAtMs = json.getLong("startedAtMs"),
                durationMs = json.getLong("durationMs"),
                starter = json.optIntOrNull("starter"),
                winner = json.optIntOrNull("winner"),
                players = (0 until players.length()).map { i ->
                    val p = players.getJSONObject(i)
                    Player(
                        name = p.getString("name"),
                        profileId = p.optStringOrNull("profileId"),
                        finalLife = p.getInt("finalLife"),
                        turnsCompleted = p.getInt("turnsCompleted"),
                        outOrder = p.optIntOrNull("outOrder"),
                        commanderDamageReceived = p.optInt("commanderDamageReceived"),
                    )
                },
            )
        }.getOrNull()
    }
}

internal fun JSONObject.optIntOrNull(key: String): Int? = if (!has(key) || isNull(key)) null else getInt(key)

internal fun JSONObject.optLongOrNull(key: String): Long? = if (!has(key) || isNull(key)) null else getLong(key)

internal fun JSONObject.optStringOrNull(key: String): String? = if (!has(key) || isNull(key)) null else getString(key)
