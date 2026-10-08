package com.turnhub.android.standalone

import com.turnhub.android.protocol.GameProfile
import org.json.JSONArray
import org.json.JSONObject

/**
 * An immutable finished local match, retained independently of Atlas delivery.
 * Local identity is [Player.localId]; an old unscoped profileId is not a link.
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
        val turnMs: Long = 0,
        val fastestTurnMs: Long = 0,
        val longestTurnMs: Long = 0,
        /** 1 for the first player out; null for whoever was still in at the end. */
        val outOrder: Int?,
        val commanderDamageReceived: Int,
        val localId: String? = null,
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
                    put("localId", p.localId ?: JSONObject.NULL)
                    put("finalLife", p.finalLife)
                    put("turnsCompleted", p.turnsCompleted)
                    put("turnMs", p.turnMs)
                    put("fastestTurnMs", p.fastestTurnMs)
                    put("longestTurnMs", p.longestTurnMs)
                    put("outOrder", p.outOrder ?: JSONObject.NULL)
                    put("commanderDamageReceived", p.commanderDamageReceived)
                })
            }
        })
    }

    /**
     * The `/api/standalone/import` form fields (protocol/http-v1.md): one game,
     * players numbered from 0 in turn order.
     */
    fun importFields(atlasId: String, profileIds: List<String>): List<Pair<String, String>> = buildList {
        require(atlasId.isNotBlank() && profileIds.size == players.size && profileIds.all { it.isNotBlank() })
        add("atlasId" to atlasId)
        add("recordId" to recordId)
        add("gameProfile" to profile.wireValue)
        add("durationMs" to "${durationMs.coerceIn(0, MAX_DURATION_MS)}")
        add("players" to "${players.size}")
        add("starter" to (starter?.toString() ?: ""))
        add("winner" to (winner?.toString() ?: ""))
        players.forEachIndexed { i, p ->
            add("name$i" to p.name)
            add("profile$i" to profileIds[i])
            add("turns$i" to "${p.turnsCompleted}")
            add("turnMs$i" to "${p.turnMs.coerceIn(0, MAX_DURATION_MS)}")
            add("fastest$i" to "${p.fastestTurnMs.coerceIn(0, DAY_MS)}")
            add("longest$i" to "${p.longestTurnMs.coerceIn(0, DAY_MS)}")
            add("out$i" to "${p.outOrder ?: 0}")
        }
    }

    companion object {
        private const val DAY_MS = 86_400_000L
        /** Atlas takes up to a week per game. */
        private const val MAX_DURATION_MS = 7 * DAY_MS

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
                        localId = p.optStringOrNull("localId"),
                        profileId = p.optStringOrNull("profileId"),
                        finalLife = p.getInt("finalLife"),
                        turnsCompleted = p.getInt("turnsCompleted"),
                        turnMs = p.optLong("turnMs"),
                        fastestTurnMs = p.optLong("fastestTurnMs"),
                        longestTurnMs = p.optLong("longestTurnMs"),
                        outOrder = p.optIntOrNull("outOrder"),
                        commanderDamageReceived = p.optInt("commanderDamageReceived"),
                    )
                },
            ).also { record ->
                require(record.recordId.isNotBlank() && record.players.size in 2..StandaloneGame.MAX_PLAYERS)
                require(record.winner == null || record.winner in record.players.indices)
                require(record.starter == null || record.starter in record.players.indices)
            }
        }.getOrNull()
    }
}

internal fun JSONObject.optIntOrNull(key: String): Int? = if (!has(key) || isNull(key)) null else getInt(key)

internal fun JSONObject.optLongOrNull(key: String): Long? = if (!has(key) || isNull(key)) null else getLong(key)

internal fun JSONObject.optStringOrNull(key: String): String? = if (!has(key) || isNull(key)) null else getString(key)
