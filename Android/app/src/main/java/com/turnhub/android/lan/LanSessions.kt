package com.turnhub.android.lan

import com.turnhub.android.domain.TileBackground
import com.turnhub.android.domain.TileBackgroundStyle
import com.turnhub.android.standalone.LocalPersonalization
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
    /** Each seat's color, avatar and panel background, in player order (null: the default look). */
    private val looks: () -> List<LocalPersonalization?> = { emptyList() },
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

    /** The looks version the current request already holds; images are sent only when it changes. */
    private var heldLooks = ""

    fun handle(request: JSONObject): JSONObject = try {
        heldLooks = request.optString("looksId")
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
        // Only this public match and the seats' looks leave the host; never its library,
        // credentials, profile links, historical games or local account IDs. The commander
        // printing is a public card reference, so guests draw the same commander art.
        val public = current.copy(planechase = com.turnhub.android.standalone.Planechase(),
            players = current.players.map { it.copy(localId = null, profileId = null) })
        val reply = reply(code).put("game", StandaloneTable.gameToJson(public))
            .put("claimed", org.json.JSONArray(guests.map { it.player }.filter { it > 0 }))
        val (id, encoded) = publicLooks(current.players.size)
        reply.put("looksId", id)
        if (id != heldLooks) reply.put("looks", encoded)
        return reply
    }

    /**
     * The seats' public looks, so a guest draws the same panels as the host. Only color, avatar
     * and background leave the host; images fall back to a gradient of the seat's color when
     * they would not fit comfortably in one frame.
     */
    private fun publicLooks(count: Int): Pair<String, org.json.JSONArray> {
        val seats = looks().take(count).let { it + List(count - it.size) { null } }
        fun encode(images: Boolean) = org.json.JSONArray(seats.map { look ->
            look?.let { full ->
                val background = full.tileBackground.let { b ->
                    if (images || b.style != TileBackgroundStyle.IMAGE) b else TileBackground(TileBackgroundStyle.GRADIENT, b.color ?: full.color)
                }.let { if (it.style == TileBackgroundStyle.IMAGE) it else it.copy(image = null) }
                LocalPersonalization(full.color, full.avatar, full.image?.takeIf { images }, tileBackground = background).toJson()
            } ?: JSONObject.NULL
        })
        val encoded = encode(true).takeIf { it.toString().length <= MAX_LOOKS } ?: encode(false)
        val digest = java.security.MessageDigest.getInstance("SHA-256").digest(encoded.toString().toByteArray(Charsets.UTF_8))
        return digest.take(12).joinToString("") { "%02x".format(it) } to encoded
    }

    private fun reply(code: String) = JSONObject().put("v", LAN_VERSION).put("session", session).put("code", code)

    private companion object {
        const val MAX_FAILURES = 10
        /** Looks share the 512 KiB frame with the game itself. */
        const val MAX_LOOKS = 320 * 1024
    }
}
