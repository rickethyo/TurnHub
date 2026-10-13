package com.turnhub.android.standalone

import org.json.JSONArray
import org.json.JSONObject

/** Device-local identity. Two players may deliberately have the same display name. */
data class SavedLocalPlayer(val localId: String, val name: String)

enum class DeliveryStatus { NEEDS_LINKING, PENDING, IMPORTED, REJECTED }

/** Delivery metadata never owns or removes the canonical match snapshot. */
data class MatchDelivery(
    val recordId: String,
    val status: DeliveryStatus = DeliveryStatus.NEEDS_LINKING,
    val reason: String? = null,
    val atlasId: String? = null,
    /** Profile IDs in the match's immutable player order. */
    val profileIds: List<String> = emptyList(),
)

data class LocalTotals(val played: Int, val won: Int, val draws: Int)

/** One device player's statistics from retained local matches, in the shape of Atlas's profile stats. */
data class LocalStats(
    val totals: LocalTotals,
    val started: Int,
    val eliminated: Int,
    val conceded: Int,
    val turnsCompleted: Int,
    val averageTurnMs: Long,
    val fastestTurnMs: Long,
    val longestTurnMs: Long,
    val averageGameMs: Long,
)

data class LocalLibrary(
    val players: List<SavedLocalPlayer> = emptyList(),
    val history: List<GameRecord> = emptyList(),
    val deliveries: List<MatchDelivery> = emptyList(),
) {
    fun totals(localId: String): LocalTotals {
        val matches = history.filter { r -> r.players.any { it.localId == localId } }
        return LocalTotals(matches.size, matches.count { r -> r.winner?.let { r.players[it].localId == localId } == true },
            matches.count { it.winner == null })
    }

    fun stats(localId: String): LocalStats {
        val seats = history.mapNotNull { r ->
            r.players.indexOfFirst { it.localId == localId }.takeIf { it >= 0 }?.let { r to it }
        }
        val mine = seats.map { (r, i) -> r.players[i] }
        val turns = mine.sumOf { it.turnsCompleted }
        val turnMs = mine.sumOf { it.turnMs }
        return LocalStats(
            totals = totals(localId),
            started = seats.count { (r, i) -> r.starter == i },
            eliminated = mine.count { it.outOrder != null && !it.conceded },
            conceded = mine.count { it.conceded },
            turnsCompleted = turns,
            averageTurnMs = if (turns > 0) turnMs / turns else 0,
            fastestTurnMs = mine.map { it.fastestTurnMs }.filter { it > 0 }.minOrNull() ?: 0,
            longestTurnMs = mine.maxOfOrNull { it.longestTurnMs } ?: 0,
            averageGameMs = if (seats.isNotEmpty()) seats.sumOf { it.first.durationMs } / seats.size else 0,
        )
    }

    fun delivery(recordId: String): MatchDelivery = deliveries.firstOrNull { it.recordId == recordId }
        ?: MatchDelivery(recordId)

    fun withDelivery(delivery: MatchDelivery): LocalLibrary =
        copy(deliveries = deliveries.filterNot { it.recordId == delivery.recordId } + delivery)

    fun withRecord(record: GameRecord): LocalLibrary = if (history.any { it.recordId == record.recordId }) this
        else copy(history = history + record).withDelivery(MatchDelivery(record.recordId))

    fun toJson(): JSONObject = JSONObject().apply {
        put("schema", SCHEMA)
        put("players", JSONArray().apply {
            players.forEach { put(JSONObject().put("localId", it.localId).put("name", it.name)) }
        })
        put("history", JSONArray().apply { history.forEach { put(it.toJson()) } })
        put("deliveries", JSONArray().apply {
            deliveries.forEach { d -> put(JSONObject().apply {
                put("recordId", d.recordId)
                put("status", d.status.name)
                put("reason", d.reason ?: JSONObject.NULL)
                put("atlasId", d.atlasId ?: JSONObject.NULL)
                put("profileIds", JSONArray(d.profileIds))
            }) }
        })
    }

    companion object {
        const val SCHEMA = 1
        fun fromJson(json: JSONObject): LocalLibrary {
            require(json.getInt("schema") == SCHEMA) { "Unsupported local library schema" }
            val players = json.getJSONArray("players")
            val history = json.getJSONArray("history")
            val deliveries = json.getJSONArray("deliveries")
            return LocalLibrary(
                players = (0 until players.length()).map { i -> players.getJSONObject(i).let {
                    SavedLocalPlayer(it.getString("localId"), it.getString("name"))
                } },
                history = (0 until history.length()).map { i ->
                    requireNotNull(GameRecord.fromJson(history.getJSONObject(i))) { "Invalid match" }
                },
                deliveries = (0 until deliveries.length()).map { i -> deliveries.getJSONObject(i).let { d ->
                    val ids = d.getJSONArray("profileIds")
                    MatchDelivery(d.getString("recordId"), DeliveryStatus.valueOf(d.getString("status")),
                        d.optStringOrNull("reason"), d.optStringOrNull("atlasId"),
                        (0 until ids.length()).map { ids.getString(it) })
                } },
            ).also { library ->
                require(library.players.all { it.localId.isNotBlank() && it.name.isNotBlank() })
                require(library.players.map { it.localId }.distinct().size == library.players.size)
                require(library.history.map { it.recordId }.distinct().size == library.history.size)
                require(library.deliveries.map { it.recordId }.distinct().size == library.deliveries.size)
                library.deliveries.forEach { delivery ->
                    val record = requireNotNull(library.history.firstOrNull { it.recordId == delivery.recordId })
                    if (delivery.status != DeliveryStatus.NEEDS_LINKING) {
                        require(!delivery.atlasId.isNullOrBlank())
                        require(delivery.profileIds.size == record.players.size && delivery.profileIds.all { it.isNotBlank() })
                        require(delivery.profileIds.distinct().size == delivery.profileIds.size)
                    }
                }
            }
        }
    }
}

/** Ephemeral profile choices fetched from one connected Atlas, never cached as local identity. */
data class LocalImportState(
    val atlasId: String? = null,
    val profiles: List<com.turnhub.android.protocol.ProfileSummary> = emptyList(),
    val busy: Boolean = false,
    val message: String? = null,
)
