package com.turnhub.android.standalone

import org.json.JSONArray
import org.json.JSONObject

/** Device-local identity. Two players may deliberately have the same display name. */
data class SavedLocalPlayer(
    val localId: String,
    val name: String,
    val profileId: String = LocalProfileIds.create(),
    val personalization: LocalPersonalization = LocalPersonalization(),
    val links: List<AtlasProfileLink> = emptyList(),
    val credential: LocalCredential? = null,
)

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
    /** Star Commander games, counted apart too: played and won (shared victories included). */
    val starPlayed: Int = 0,
    val starWon: Int = 0,
)

data class LocalLibrary(
    val players: List<SavedLocalPlayer> = emptyList(),
    val history: List<GameRecord> = emptyList(),
    val deliveries: List<MatchDelivery> = emptyList(),
) {
    /** Only a verified destination link joins local decoration to a connected player. */
    fun linkedPlayer(atlasId: String, profileId: String?): SavedLocalPlayer? =
        if (atlasId.isBlank() || profileId == null) null else players.firstOrNull { player ->
            player.links.any { it.atlasId == atlasId && it.profileId == profileId }
        }

    fun decorateTable(summary: com.turnhub.android.domain.TableSummary): com.turnhub.android.domain.TableSummary =
        summary.copy(players = summary.players.map { player ->
            linkedPlayer(summary.atlasId, player.profileId)?.let { local ->
                player.copy(tileBackground = local.personalization.tileBackground)
            } ?: player
        })

    fun totals(localId: String): LocalTotals {
        val matches = history.filter { r -> r.players.any { it.localId == localId } }
        return LocalTotals(matches.size, matches.count { r -> r.won(r.players.indexOfFirst { it.localId == localId }) },
            matches.count { it.draw })
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
            starPlayed = seats.count { (r, _) -> r.profile.starCommander },
            starWon = seats.count { (r, i) -> r.profile.starCommander && r.won(i) },
        )
    }

    fun profileStatistics(localId: String): com.turnhub.android.data.ProfileStatistics {
        val player = requireNotNull(players.firstOrNull { it.localId == localId })
        val seats = history.mapNotNull { r -> r.players.indexOfFirst { it.localId == localId }
            .takeIf { it >= 0 }?.let { r to it } }
        val stats = stats(localId)
        val last = seats.lastOrNull()
        val seat = last?.let { it.first.players[it.second] }
        val count = stats.totals.played.toLong()
        return com.turnhub.android.data.ProfileStatistics(player.name, player.profileId,
            String.format(java.util.Locale.ROOT, "%.1f%%", if (count == 0L) 0.0 else stats.totals.won * 100.0 / count), true,
            com.turnhub.android.data.ProfileStatistics.Lifetime(count, stats.totals.won.toLong(), stats.started.toLong(),
                stats.eliminated.toLong(), stats.turnsCompleted.toLong(), seats.sumOf { it.first.players[it.second].turnMs },
                stats.averageTurnMs, stats.fastestTurnMs, stats.longestTurnMs, seats.sumOf { it.first.durationMs },
                stats.averageGameMs, stats.conceded.toLong()),
            com.turnhub.android.data.ProfileStatistics.LastGame(when {
                last == null -> "None"
                last.first.draw -> "Draw"
                last.first.won(last.second) -> "Win"
                seat?.conceded == true -> "Conceded"
                seat?.outOrder != null -> "Eliminated"
                else -> "Loss"
            }, last?.first?.durationMs ?: 0, seat?.turnsCompleted?.toLong() ?: 0,
                if (seat != null && seat.turnsCompleted > 0) seat.turnMs / seat.turnsCompleted else 0,
                seat?.fastestTurnMs ?: 0, seat?.longestTurnMs ?: 0),
            com.turnhub.android.data.ProfileStatistics.Moderation(false, 0, 0, "Local games have no remote moderation."))
    }

    fun delivery(recordId: String): MatchDelivery = deliveries.firstOrNull { it.recordId == recordId }
        ?: MatchDelivery(recordId)

    /** Only confirmed links, checked against current Atlas profiles, enable automatic credit. */
    fun automaticMapping(record: GameRecord, atlasId: String, available: Set<String>): List<String>? {
        val ids = record.players.map { seat ->
            val player = players.firstOrNull { it.localId == seat.localId } ?: return null
            val link = player.links.firstOrNull { it.atlasId == atlasId } ?: return null
            if (link.profileId !in available) return null
            link.profileId
        }
        return ids.takeIf { it.distinct().size == it.size }
    }

    fun withDelivery(delivery: MatchDelivery): LocalLibrary =
        copy(deliveries = deliveries.filterNot { it.recordId == delivery.recordId } + delivery)

    fun withRecord(record: GameRecord): LocalLibrary = if (history.any { it.recordId == record.recordId }) this
        else copy(history = history + record).withDelivery(MatchDelivery(record.recordId))

    fun toJson(): JSONObject = JSONObject().apply {
        put("schema", SCHEMA)
        put("players", JSONArray().apply {
            players.forEach { p -> put(JSONObject().put("localId", p.localId).put("name", p.name)
                .put("profileId", p.profileId).put("personalization", p.personalization.toJson())
                .put("links", JSONArray().apply { p.links.forEach { put(JSONObject()
                    .put("atlasId", it.atlasId).put("profileId", it.profileId)) } })
                .put("credential", p.credential?.let { JSONObject().put("salt", it.salt).put("hash", it.hash) } ?: JSONObject.NULL)) }
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
        const val SCHEMA = 2
        fun fromJson(json: JSONObject): LocalLibrary {
            require(json.getInt("schema") in 1..SCHEMA) { "Unsupported local library schema" }
            val players = json.getJSONArray("players")
            val history = json.getJSONArray("history")
            val deliveries = json.getJSONArray("deliveries")
            return LocalLibrary(
                players = (0 until players.length()).map { i -> players.getJSONObject(i).let {
                    val links = it.optJSONArray("links") ?: JSONArray()
                    SavedLocalPlayer(it.getString("localId"), it.getString("name"),
                        it.optString("profileId").ifBlank { LocalProfileIds.create() },
                        it.optJSONObject("personalization")?.let(LocalPersonalization::fromJson) ?: LocalPersonalization(),
                        (0 until links.length()).map { i -> links.getJSONObject(i).let { l ->
                            AtlasProfileLink(l.getString("atlasId"), l.getString("profileId")) } },
                        it.optJSONObject("credential")?.let { c -> LocalCredential(c.getString("salt"), c.getString("hash")) })
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
                require(library.players.map { it.profileId }.distinct().size == library.players.size)
                require(library.players.all { p -> p.credential?.let { c ->
                    java.util.Base64.getDecoder().decode(c.salt).size == 16 && java.util.Base64.getDecoder().decode(c.hash).size == 32
                } ?: true })
                require(library.players.all { LocalProfileIds.valid(it.profileId) && it.links.all { l ->
                    l.atlasId.isNotBlank() && LocalProfileIds.valid(l.profileId) } &&
                    it.links.map { l -> l.atlasId }.distinct().size == it.links.size })
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
