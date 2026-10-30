package com.turnhub.android.standalone

import org.json.JSONArray
import org.json.JSONObject

/**
 * "Use on another device" (owner, 2026-10-29): one person's account copied between their own
 * phones and tablets, so each can mark the same account (same profile ID) as its device Admin.
 * Carries identity, original creation time, look, links, PIN verifier and that player's
 * retained match history. Match UUIDs deduplicate repeated copies and Atlas delivery.
 */
object AccountCopy {
    fun toJson(player: SavedLocalPlayer, history: List<GameRecord> = emptyList()): JSONObject = JSONObject()
        .put("copySchema", 2)
        .put("profileId", player.profileId)
        .put("name", player.name)
        .put("personalization", player.personalization.toJson())
        .put("lastActivityMs", player.lastActivityMs)
        .put("createdAtMs", player.createdAtMs)
        .put("history", JSONArray().apply {
            history.filter { record -> record.players.any { it.localId == player.localId } }.forEach { record ->
                put(record.copy(players = record.players.map { seat ->
                    if (seat.localId == player.localId) seat.copy(localId = null, profileId = player.profileId)
                    else seat.copy(localId = null)
                }).toJson())
            }
        })
        .put("credential", player.credential?.let { JSONObject().put("salt", it.salt).put("hash", it.hash) } ?: JSONObject.NULL)
        .put("links", JSONArray().apply { player.links.forEach { put(JSONObject().put("atlasId", it.atlasId)
            .put("profileId", it.profileId).put("linkKey", it.linkKey ?: JSONObject.NULL)) } })

    /** The copied account under a new local ID; throws when the copy is not a valid account. */
    fun fromJson(json: JSONObject, localId: String): SavedLocalPlayer {
        val name = json.getString("name").trim()
        require(name.isNotBlank() && name.length <= StandaloneGame.MAX_NAME)
        val profileId = json.getString("profileId")
        require(LocalProfileIds.valid(profileId))
        val credential = json.optJSONObject("credential")?.let { LocalCredential(it.getString("salt"), it.getString("hash")) }
        credential?.let {
            require(java.util.Base64.getDecoder().decode(it.salt).size == 16 && java.util.Base64.getDecoder().decode(it.hash).size == 32)
        }
        val array = json.optJSONArray("links") ?: JSONArray()
        val links = (0 until minOf(array.length(), 32)).map { i -> array.getJSONObject(i).let {
            AtlasProfileLink(it.getString("atlasId"), it.getString("profileId"), it.optStringOrNull("linkKey"))
        } }
        require(links.all { it.atlasId.isNotBlank() && LocalProfileIds.valid(it.profileId) &&
            (it.linkKey == null || it.linkKey.matches(Regex("[0-9A-F]{32}"))) })
        require(links.map { it.atlasId }.distinct().size == links.size)
        return SavedLocalPlayer(localId, name, profileId, LocalPersonalization.fromJson(json.getJSONObject("personalization")),
            links, credential, json.optLong("lastActivityMs", 0), json.optLong("createdAtMs", 0).also { require(it >= 0) })
    }

    /** Only the copied identity gains a local binding; names and opponents' device UUIDs never do. */
    fun history(json: JSONObject, owner: SavedLocalPlayer): List<GameRecord> {
        val records = json.optJSONArray("history") ?: return emptyList()
        return (0 until records.length()).map { index ->
            val record = requireNotNull(GameRecord.fromJson(records.getJSONObject(index))) { "Invalid copied match" }
            require(record.players.count { it.profileId == owner.profileId } == 1) { "Copied match must belong to this profile" }
            record.copy(players = record.players.map { seat -> seat.copy(localId =
                if (seat.profileId == owner.profileId) owner.localId else null) })
        }.also { list -> require(list.map { it.recordId }.distinct().size == list.size) { "Duplicate match IDs in copy" } }
    }
}
