package com.turnhub.android.lan

import com.turnhub.android.protocol.TableState
import com.turnhub.android.standalone.StandaloneGame
import com.turnhub.android.standalone.StandaloneTable
import org.json.JSONObject

/** Called on the host's main dispatcher, alongside all local table edits. */
internal class LanSessions(
    private val session: String,
    private val key: String,
    private val game: () -> StandaloneGame,
    private val apply: (LanAction) -> Boolean,
    private val unavailable: () -> Boolean = { false },
    private val now: () -> Long = { System.nanoTime() / 1_000_000 },
    private val secret: () -> String = { randomLanSecret() },
) {
    private val match = game().gameId
    private data class Receipt(val payload: String, val code: String)
    private data class Client(val guest: LanGuest, val joinId: String, var seen: Long,
        val receipts: LinkedHashMap<String, Receipt> = linkedMapOf())
    private val clients = linkedMapOf<String, Client>()
    val guests: List<LanGuest> get() = clients.values.map { it.guest }
    fun revoke(token: String) { clients.remove(token) }

    fun handle(request: JSONObject): JSONObject = try {
        require(request.integer("v") == LAN_VERSION.toLong() && request.text("session") == session)
        val current = game()
        if (!current.profile.deathMayDie || current.gameId != match || current.state == TableState.LOBBY)
            reply("ended")
        else if (unavailable()) reply("unavailable")
        else when (request.text("op")) {
            "preview", "join" -> {
                if (!sameSecret(request.text("key"), key)) reply("unauthorized")
                else if (request.text("op") == "preview") snapshot("ok")
                else join(request)
            }
            else -> {
                val client = clients[request.text("token")]
                if (client == null) reply("unauthorized")
                else {
                    client.seen = now()
                    when (request.text("op")) {
                        "poll" -> snapshot("ok")
                        "leave" -> { clients.remove(client.guest.token); reply("ok") }
                        "command" -> command(client, request)
                        else -> reply("invalid")
                    }
                }
            }
        }
    } catch (_: Exception) { reply("invalid") }

    private fun join(request: JSONObject): JSONObject {
        val id = request.text("joinId")
        require(id.matches(Regex("[0-9a-f-]{36}")))
        val role = request.text("role")
        val player = request.integer("player").also { require(it in 0..10) }.toInt()
        require(role == "elder" && player == 0 || role == "investigator" && player in 1..game().players.size)
        clients.entries.removeAll { now() - it.value.seen > 5 * 60_000 }
        val repeated = clients.values.firstOrNull { it.joinId == id }
        if (repeated != null) {
            require(repeated.guest.role == role && repeated.guest.player == player)
            repeated.seen = now()
            return snapshot("ok").put("token", repeated.guest.token)
        }
        if (clients.size >= 16) return reply("full")
        if (player != 0 && clients.values.any { it.guest.player == player }) return snapshot("claimed")
        val token = secret()
        clients[token] = Client(LanGuest(token, role, player), id, now())
        return snapshot("ok").put("token", token)
    }

    private fun command(client: Client, request: JSONObject): JSONObject {
        if (client.guest.role != "investigator") return reply("forbidden")
        val id = request.text("actionId")
        require(id.matches(Regex("[0-9a-f-]{36}")))
        val kind = request.text("kind")
        require(kind.length <= 24)
        val value = request.integer("value").also { require(it in -20..20) }.toInt()
        val slot = request.integer("slot").also { require(it in 0..10) }.toInt()
        val revision = request.integer("revision")
        val requestedMatch = request.text("match")
        require(requestedMatch.length in 1..64)
        val payload = listOf(requestedMatch, revision, kind, value, slot).joinToString("|")
        client.receipts[id]?.let { return if (it.payload == payload) snapshot(it.code) else reply("invalid") }
        // Never evict receipts and accidentally replay an old delta. Start a new
        // authenticated client session when its bounded command budget is used.
        if (client.receipts.size >= 2048) return snapshot("renew")
        val current = game()
        val code = if (requestedMatch != match || revision != current.revision) "stale"
            else if (apply(LanAction(match, revision, client.guest.player, kind, value, slot))) "ok" else "rejected"
        client.receipts[id] = Receipt(payload, code)
        return snapshot(code)
    }

    private fun snapshot(code: String): JSONObject {
        val current = game()
        // Only this public match leaves the host; never its library, credentials,
        // profile links, historical games, player artwork or local account IDs.
        val public = current.copy(planechase = com.turnhub.android.standalone.Planechase(),
            players = current.players.map { it.copy(localId = null, profileId = null, commander = null, commanderArt = null) })
        return reply(code).put("game", StandaloneTable.gameToJson(public))
            .put("claimed", org.json.JSONArray(guests.map { it.player }.filter { it > 0 }))
    }

    private fun reply(code: String) = JSONObject().put("v", LAN_VERSION).put("session", session).put("code", code)
}
