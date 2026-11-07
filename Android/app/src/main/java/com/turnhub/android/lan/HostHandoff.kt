package com.turnhub.android.lan

import com.turnhub.android.standalone.StandaloneTable
import org.json.JSONObject

/** Durable source-side offer. Only the selected, authenticated room member may collect it. */
internal class HostHandoff(private val record: JSONObject, private val save: () -> Boolean,
    private val table: StandaloneTable) {
    val pending: Boolean get() = record.optJSONObject("handoff") != null
    val released: Boolean get() = record.optJSONObject("handoff")?.let {
        it.optBoolean("released") || table.state.value.game.receivedTransfer == "released:${it.getString("id")}" } == true
    val collected: Boolean get() = record.optJSONObject("handoff")?.optBoolean("collected") == true
    fun collectedBy(token: String, id: String): Boolean {
        val offer = record.optJSONObject("handoff") ?: return false
        if (!released || offerFor(token) != id) return false
        offer.put("collected", true)
        return save()
    }
    fun offer(token: String): Boolean {
        if (pending) return false
        val id = randomLanSecret(16)
        val state = table.state.value
        val json = StandaloneTable.gameToJson(state.game)
        val players = json.getJSONArray("players")
        state.game.players.forEachIndexed { index, player ->
            val profile = state.library.players.firstOrNull { it.localId == player.localId }?.profileId ?: player.profileId
            players.getJSONObject(index).put("profileId", profile ?: JSONObject.NULL).put("localId", JSONObject.NULL)
        }
        record.put("handoff", JSONObject().put("id", id).put("target", token).put("game", json))
        if (!save()) { record.remove("handoff"); return false }
        // A persisted offer is not usable until this authority fence also reaches storage.
        return table.prepareHostTransfer(id)
    }
    fun cancel(): Boolean {
        val offer = record.optJSONObject("handoff") ?: return true
        if (released) return false
        record.remove("handoff")
        if (!save()) { record.put("handoff", offer); return false }
        return table.cancelHostTransfer(offer.getString("id"))
    }
    fun offerFor(token: String): String? = record.optJSONObject("handoff")?.takeIf {
        sameSecret(it.getString("target"), token)
    }?.getString("id")
    fun accept(token: String, id: String): JSONObject? {
        val offer = record.optJSONObject("handoff") ?: return null
        if (offerFor(token) != id) return null
        // Also fences a recovered offer after a process stopped between the two writes.
        if (!offer.optBoolean("released") && table.state.value.game.receivedTransfer != "released:$id" && !table.prepareHostTransfer(id)) return null
        if (!table.releaseTransferredGame(id)) return null
        offer.put("released", true)
        if (!save()) return null
        return JSONObject(offer.getJSONObject("game").toString())
    }
}
