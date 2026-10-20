package com.turnhub.android.lan

import com.turnhub.android.standalone.StandaloneGame
import com.turnhub.android.standalone.StandaloneTable
import org.json.JSONObject

/**
 * Called on the host's main dispatcher, alongside all local table edits. One sharing
 * session lasts while the host keeps sharing the same format: through its lobby, the match
 * and any rematch. Guests may join in the lobby; their edits apply only while a match runs.
 */
internal class LanSessions(
    private val session: String,
    private val key: String,
    private val game: () -> StandaloneGame,
    private val apply: (LanAction) -> Boolean,
    private val unavailable: () -> Boolean = { false },
    private val now: () -> Long = { System.nanoTime() / 1_000_000 },
    private val secret: () -> String = { randomLanSecret() },
) {
    private val sharedProfile = game().profile
    private data class Receipt(val payload: String, val code: String)
    private data class Client(val guest: LanGuest, val joinId: String, var seen: Long,
        val receipts: LinkedHashMap<String, Receipt> = linkedMapOf())
    private val clients = linkedMapOf<String, Client>()
    /** Investigators whose player left the lobby: told once, then forgotten. */
    private val released = mutableSetOf<String>()
    /** Wrong keys in the last minute. A short join code must not be guessable by trying them all. */
    private val failures = ArrayDeque<Long>()
    val guests: List<LanGuest> get() { reseat(); return clients.values.map { it.guest } }
    fun revoke(token: String) { clients.remove(token) }

    /** Follow each investigator's player by name as the lobby adds, removes or reorders players. */
    private fun reseat() {
        val players = game().players
        clients.values.toList().forEach { client ->
            val name = client.guest.name ?: return@forEach
            val at = players.indexOfFirst { it.name == name }
            if (at < 0) { clients.remove(client.guest.token); released += client.guest.token }
            else if (at + 1 != client.guest.player) clients[client.guest.token] = client.copy(guest = client.guest.copy(player = at + 1))
        }
    }

    private fun keyAccepted(offered: String): Boolean {
        val t = now()
        while (failures.isNotEmpty() && t - failures.first() > 60_000) failures.removeFirst()
        if (failures.size >= MAX_FAILURES) return false
        if (sameSecret(offered, key)) return true
        failures.addLast(t)
        return false
    }

    fun handle(request: JSONObject): JSONObject = try {
        require(request.integer("v") == LAN_VERSION.toLong() && request.text("session") == session)
        reseat()
        if (!sharedProfile.lanSupported || game().profile != sharedProfile) reply("ended")
        else if (unavailable()) reply("unavailable")
        else when (request.text("op")) {
            "preview", "join" -> {
                if (failures.size >= MAX_FAILURES && now() - failures.first() <= 60_000) reply("locked")
                else if (!keyAccepted(request.text("key"))) reply(if (failures.size >= MAX_FAILURES) "locked" else "unauthorized")
                else if (request.text("op") == "preview") snapshot("ok")
                else join(request)
            }
            else -> {
                val token = request.text("token")
                val client = clients[token]
                if (client == null) reply(if (released.remove(token)) "released" else "unauthorized")
                else {
                    client.seen = now()
                    when (request.text("op")) {
                        "poll" -> snapshot("ok").put("player", client.guest.player)
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
        val players = game().players
        require(if (sharedProfile.deathMayDie) role == "elder" && player == 0 || role == "investigator" && player in 1..players.size
            else role == "player" && player in 1..players.size)
        val name = if (player == 0) null else players[player - 1].name
        clients.entries.removeAll { now() - it.value.seen > 5 * 60_000 }
        val repeated = clients.values.firstOrNull { it.joinId == id }
        if (repeated != null) {
            require(repeated.guest.role == role && repeated.guest.name == name)
            repeated.seen = now()
            return snapshot("ok").put("token", repeated.guest.token).put("player", repeated.guest.player)
        }
        if (clients.size >= 16) return reply("full")
        if (player != 0 && clients.values.any { it.guest.player == player }) return snapshot("claimed")
        val token = secret()
        clients[token] = Client(LanGuest(token, role, player, name), id, now())
        return snapshot("ok").put("token", token).put("player", player)
    }

    private fun command(client: Client, request: JSONObject): JSONObject {
        if (client.guest.role != if (sharedProfile.deathMayDie) "investigator" else "player") return reply("forbidden")
        val id = request.text("actionId")
        require(id.matches(Regex("[0-9a-f-]{36}")))
        val kind = request.text("kind")
        require(kind.length <= 64)
        val value = request.integer("value").also { require(it in -100_000..100_000) }.toInt()
        val slot = request.integer("slot").also { require(it in 0..10) }.toInt()
        val source = if (request.has("source")) request.integer("source").also { require(it in 0..10) }.toInt() else 0
        val revision = request.integer("revision")
        val requestedMatch = request.text("match")
        require(requestedMatch.length in 1..64)
        val payload = listOf(requestedMatch, revision, kind, value, slot, source).joinToString("|")
        client.receipts[id]?.let { return if (it.payload == payload) snapshot(it.code).put("player", client.guest.player) else reply("invalid") }
        // Never evict receipts and accidentally replay an old delta. Start a new
        // authenticated client session when its bounded command budget is used.
        if (client.receipts.size >= 2048) return snapshot("renew")
        val current = game()
        val code = if (requestedMatch != current.gameId || revision != current.revision) "stale"
            else if (apply(LanAction(current.gameId, revision, client.guest.player, kind, value, slot, source))) "ok" else "rejected"
        client.receipts[id] = Receipt(payload, code)
        return snapshot(code).put("player", client.guest.player)
    }

    private fun snapshot(code: String): JSONObject {
        val current = game()
        // Only this public match leaves the host; never its library, credentials,
        // profile links, historical games, player artwork or local account IDs.
        val public = current.copy(planechase = com.turnhub.android.standalone.Planechase(),
            players = current.players.map { it.copy(localId = null, profileId = null, commanderArt = null) })
        return reply(code).put("game", StandaloneTable.gameToJson(public))
            .put("claimed", org.json.JSONArray(guests.map { it.player }.filter { it > 0 }))
    }

    private fun reply(code: String) = JSONObject().put("v", LAN_VERSION).put("session", session).put("code", code)

    private companion object { const val MAX_FAILURES = 10 }
}
