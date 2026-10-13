package com.turnhub.android.standalone

import com.turnhub.android.protocol.GameProfile
import org.json.JSONArray
import org.json.JSONObject

/**
 * An immutable finished local match, retained independently of Atlas delivery.
 * Local identity is [Player.localId]; an old unscoped profileId is not a link.
 * Player indices ([starter], [winner], [winners]) are positions in [players],
 * which is turn order.
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
    /** Teams of neighbours (players 1+2, 3+4, ...): the winner's teammate won too. */
    val twoHeadedGiant: Boolean = false,
    /** Every winner, [winner] first: several share a Star Commander victory. */
    val winners: List<Int> = listOfNotNull(winner),
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
        val conceded: Boolean = false,
        val counters: com.turnhub.android.protocol.PlayerCounters = com.turnhub.android.protocol.PlayerCounters(),
        val commanderBySource: Map<Int, List<Int>> = emptyMap(),
    )

    /** Whether player [index] won: any winner, or (Two-Headed Giant) the winner's teammate. */
    fun won(index: Int): Boolean = (winners.ifEmpty { listOfNotNull(winner) }).any { w ->
        index == w || (twoHeadedGiant && index / 2 == w / 2)
    }

    /** No winner at all. A shared victory is never a draw. */
    val draw: Boolean get() = winner == null && winners.isEmpty()

    fun toJson(): JSONObject = JSONObject().apply {
        if (twoHeadedGiant) put("twoHeadedGiant", true)
        put("recordId", recordId)
        put("gameProfile", profile.wireValue)
        put("startingLife", startingLife)
        put("startedAtMs", startedAtMs)
        put("durationMs", durationMs)
        put("starter", starter ?: JSONObject.NULL)
        put("winner", winner ?: JSONObject.NULL)
        put("winners", JSONArray(winners))
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
                    put("conceded", p.conceded)
                    put("commanderDamageReceived", p.commanderDamageReceived)
                    put("counters", JSONObject().put("poison", p.counters.poison).put("energy", p.counters.energy)
                        .put("experience", p.counters.experience).put("commanderCasts", JSONArray(p.counters.commanderCasts))
                        .put("cityBlessing", p.counters.cityBlessing))
                    put("commanderBySource", JSONObject().apply { p.commanderBySource.forEach { (key, values) -> put("$key", JSONArray(values)) } })
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
        // Every winner, comma-separated; a shared Star Commander victory has several.
        add("winners" to winners.joinToString(","))
        players.forEachIndexed { i, p ->
            add("name$i" to p.name)
            add("profile$i" to profileIds[i])
            add("turns$i" to "${p.turnsCompleted}")
            add("turnMs$i" to "${p.turnMs.coerceIn(0, MAX_DURATION_MS)}")
            add("fastest$i" to "${p.fastestTurnMs.coerceIn(0, DAY_MS)}")
            add("longest$i" to "${p.longestTurnMs.coerceIn(0, DAY_MS)}")
            add("out$i" to "${p.outOrder ?: 0}")
            add("conceded$i" to if (p.conceded) "1" else "0")
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
                // Records saved before shared victories: the single winner, if any.
                winners = json.optIntList("winners") ?: listOfNotNull(json.optIntOrNull("winner")),
                twoHeadedGiant = json.optBoolean("twoHeadedGiant"),
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
                        conceded = p.optBoolean("conceded"),
                        commanderDamageReceived = p.optInt("commanderDamageReceived"),
                        counters = p.optJSONObject("counters")?.let { c -> com.turnhub.android.protocol.PlayerCounters(
                            c.optInt("poison"), c.optInt("energy"), c.optInt("experience"),
                            c.getJSONArray("commanderCasts").let { a -> List(2) { a.getInt(it) } }, c.optBoolean("cityBlessing"))
                        } ?: com.turnhub.android.protocol.PlayerCounters(),
                        commanderBySource = p.optJSONObject("commanderBySource")?.let { d -> d.keys().asSequence().associate { key ->
                            key.toInt() to d.getJSONArray(key).let { a -> List(2) { a.getInt(it) } } } } ?: emptyMap(),
                    )
                },
            ).also { record ->
                require(record.recordId.isNotBlank() && record.players.size in 2..StandaloneGame.MAX_PLAYERS)
                require(record.winner == null || record.winner in record.players.indices)
                require(record.winners.all { it in record.players.indices } && record.winners.distinct().size == record.winners.size)
                require(record.winners.firstOrNull() == record.winner)
                require(record.starter == null || record.starter in record.players.indices)
            }
        }.getOrNull()
    }
}

internal fun JSONObject.optIntOrNull(key: String): Int? = if (!has(key) || isNull(key)) null else getInt(key)

internal fun JSONObject.optLongOrNull(key: String): Long? = if (!has(key) || isNull(key)) null else getLong(key)

internal fun JSONObject.optIntList(key: String): List<Int>? =
    if (!has(key) || isNull(key)) null else getJSONArray(key).let { a -> List(a.length()) { a.getInt(it) } }

internal fun JSONObject.optStringOrNull(key: String): String? = if (!has(key) || isNull(key)) null else getString(key)
