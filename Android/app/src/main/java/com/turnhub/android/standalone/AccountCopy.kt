package com.turnhub.android.standalone

import org.json.JSONArray
import org.json.JSONObject

/**
 * "Use on another device" (owner, 2026-10-29): one person's account copied between their own
 * phones and tablets, so each can mark the same account (same profile ID) as its device Admin.
 * Carries the ID, name, look, Atlas links and the PIN verifier, never a PIN; each device keeps
 * its own game history. Same-ID copies meet again on an Atlas, which merges their statistics.
 */
object AccountCopy {
    fun toJson(player: SavedLocalPlayer): JSONObject = JSONObject()
        .put("profileId", player.profileId)
        .put("name", player.name)
        .put("personalization", player.personalization.toJson())
        .put("lastActivityMs", player.lastActivityMs)
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
            links, credential, json.optLong("lastActivityMs", 0))
    }
}
