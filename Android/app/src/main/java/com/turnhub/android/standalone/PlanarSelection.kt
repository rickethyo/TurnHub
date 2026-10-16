package com.turnhub.android.standalone

import org.json.JSONArray
import org.json.JSONObject

/** A frozen pool choice. Null IDs means all downloaded cards; a named choice stores exact card IDs. */
data class PlanarSelection(val name: String = "All planes", val cardIds: Set<String>? = null) {
    fun pool(cards: List<PlanarCard>): List<PlanarCard> = cards.filter { cardIds == null || it.id in cardIds }
    fun available(cards: List<PlanarCard>): Boolean = cardIds == null ||
        (cardIds.isNotEmpty() && cards.map { it.id }.containsAll(cardIds) && pool(cards).any { !it.phenomenon })
    fun valid(): Boolean = name.isNotBlank() && name.length <= 64 &&
        (cardIds == null || (cardIds.isNotEmpty() && cardIds.size <= 2000 && cardIds.all { it.isNotBlank() && it.length <= 128 }))
    fun toJson() = JSONObject().put("name", name).put("cardIds", cardIds?.let { JSONArray(it.sorted()) } ?: JSONObject.NULL)
    companion object {
        fun fromJson(json: JSONObject): PlanarSelection {
            require(json.has("cardIds") && (json.isNull("cardIds") || json.optJSONArray("cardIds") != null))
            return PlanarSelection(json.getString("name"),
                json.optJSONArray("cardIds")?.let { a -> (0 until a.length()).map { a.getString(it) }.toSet() }
            ).also { require(it.valid()) { "Invalid planar selection" } }
        }
    }
}
