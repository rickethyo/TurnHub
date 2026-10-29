package com.turnhub.android.lan

import com.turnhub.android.data.TableControls
import com.turnhub.android.data.TabletSeat
import com.turnhub.android.domain.SeatKey
import com.turnhub.android.domain.TableSummary
import com.turnhub.android.domain.TableSummaryMapper
import com.turnhub.android.protocol.AtlasCapabilities
import com.turnhub.android.protocol.AtlasInfo
import com.turnhub.android.protocol.AtlasWireParser
import com.turnhub.android.protocol.TableState
import org.json.JSONArray
import org.json.JSONObject

/**
 * The Atlas game this device is connected to, as Play together relays it. One hub phone
 * or tablet talks to Atlas; the table's other phones join the hub over Wi-Fi instead of
 * each polling Atlas, which spares Atlas's radio and CPU. Devices may still connect to
 * Atlas directly as well (a second table screen, say). Atlas decides every outcome: the
 * hub only forwards a guest's tap for the guest's own seat, through its tablet grant.
 */
internal interface LanAtlasLink {
    /** The live Atlas game (null while not connected). */
    fun summary(): TableSummary?
    /** This device is the Atlas table's tablet, so it may act for a guest's seat. */
    fun canAct(): Boolean
    /** Runs [block] against Atlas for [game]; true when Atlas applied it. */
    suspend fun act(game: Int, block: suspend TableControls.() -> Boolean): Boolean
}

/**
 * One relayed snapshot: Atlas's own public `/api/v1/state` body, parsed again by each
 * guest with the same parser the hub uses, plus the hub's seat names and the snapshot's
 * age, so a guest's clocks run from when Atlas sent it.
 */
internal object LanAtlasFrame {
    fun encode(summary: TableSummary, nowMs: Long): JSONObject? {
        val wire = summary.stateWire ?: return null
        return JSONObject().put("state", wire).put("firmware", summary.firmwareVersion)
            .put("ageMs", (nowMs - summary.receivedAtMs).coerceIn(0, 60_000))
            .put("names", JSONArray(summary.players.filter { it.hasName }.map {
                JSONObject().put("module", it.controller.id).put("slot", it.slot).put("name", it.label)
            }))
    }

    fun decode(frame: JSONObject, nowMs: Long): TableSummary {
        val wire = frame.text("state")
        require(wire.length <= LAN_MAX_FRAME)
        val snapshot = AtlasWireParser.parseState(wire)
        val names = frame.optJSONArray("names")?.let { array ->
            (0 until minOf(array.length(), 32)).mapNotNull { i ->
                array.optJSONObject(i)?.let { SeatKey(it.integer("module").toInt(), it.integer("slot").toInt()) to it.text("name").take(64) }
            }.toMap()
        }.orEmpty()
        // The mapper reads only identity and firmware from info; the rest stays neutral.
        val info = AtlasInfo("TurnHub", "Atlas", snapshot.atlasId, frame.optString("firmware").take(32), "",
            snapshot.protocolVersion, 0, snapshot.bootId, snapshot.revision, AtlasCapabilities(true, false, false, false, false))
        val age = frame.integer("ageMs").coerceIn(0, 60_000)
        return TableSummaryMapper.map(info, snapshot.copy(wire = wire), names, receivedAtMs = nowMs - age)
    }
}

private fun relay(block: suspend TableControls.() -> Boolean) = block

/** Where a relayed command must still apply: the same Atlas boot and game. */
internal fun TableSummary.relayMatch() = "$atlasId/$bootId/$game"

/**
 * A guest's command as an Atlas tablet request for the guest's own [player]; null when
 * Play together doesn't relay it to Atlas. Mirrors the phone-hosted game's guest actions.
 */
internal fun atlasRelayAction(summary: TableSummary, player: Int, kind: String, value: Int, slot: Int, source: Int):
    (suspend TableControls.() -> Boolean)? {
    val me = summary.players.firstOrNull { it.playerNumber == player } ?: return null
    if (summary.state != TableState.RUNNING && summary.state != TableState.PAUSED) return null
    val seat = TabletSeat(me.controller.id, me.slot)
    val simple = value == 0 && slot == 0 && source == 0
    return when {
        kind == "life" && slot == 0 && source == 0 && value != 0 && value in -10_000..10_000 -> relay { life(seat, value) }
        kind == "commander" && slot in 1..2 && summary.players.any { it.playerNumber == source } && value in listOf(-1, 1) ->
            relay { commander(seat, source, slot, value); true }
        simple && kind in setOf("pass", "win", "confirm", "deny", "concede", "monarch") -> relay { control(seat, kind) }
        simple && kind == "eliminate" -> relay { eliminate(seat, null) }
        // 1 pauses and 0 resumes, so a retried or crossed tap never undoes another player's.
        kind == "pause" && slot == 0 && source == 0 && value in 0..1 ->
            if ((summary.state == TableState.RUNNING) == (value == 1)) relay { control(seat, "pause") } else null
        source == 0 && slot in 0..2 && value in -1..1 && kind in setOf("poison", "energy", "experience", "treasure", "rad",
            "commander-casts", "city-blessing", "initiative") -> relay { counter(seat, kind, value, slot.coerceAtLeast(1)); true }
        else -> null
    }
}

/**
 * The hub's side of Play together for an Atlas game; the same requests as [LanSessions],
 * whose phone-hosted game it replaces. Seats are Atlas's players; a `display` guest
 * (player 0) mirrors the table read-only. Called on the main dispatcher; a command waits
 * for Atlas's answer.
 */
internal class LanAtlasSessions(
    private val session: String,
    private val key: String,
    private val link: LanAtlasLink,
    private val unavailable: () -> Boolean = { false },
    private val now: () -> Long = { System.nanoTime() / 1_000_000 },
    private val clock: () -> Long = com.turnhub.android.domain.TableClock::nowMs,
    private val secret: () -> String = { randomLanSecret() },
) {
    /** The Atlas and game being shared; another one ends the session. */
    private val shared = link.summary()?.let { it.atlasId to it.game }
    private data class Receipt(val payload: String, val code: String)
    private data class Client(val guest: LanGuest, val joinId: String, var seen: Long,
        val receipts: LinkedHashMap<String, Receipt> = linkedMapOf())
    private val clients = linkedMapOf<String, Client>()
    private val released = mutableSetOf<String>()
    private val failures = ArrayDeque<Long>()
    val guests: List<LanGuest> get() { link.summary()?.let(::reseat); return clients.values.map { it.guest } }
    fun revoke(token: String) { clients.remove(token) }

    /** Follow each player by name when Atlas's seats move; a player who left releases their guest. */
    private fun reseat(summary: TableSummary) {
        clients.values.toList().forEach { client ->
            val name = client.guest.name ?: return@forEach
            val at = summary.players.firstOrNull { it.label == name }
            if (at == null) { clients.remove(client.guest.token); released += client.guest.token }
            else if (at.playerNumber != client.guest.player) clients[client.guest.token] = client.copy(guest = client.guest.copy(player = at.playerNumber))
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

    suspend fun handle(request: JSONObject): JSONObject = try {
        require(request.integer("v") == LAN_VERSION.toLong() && request.text("session") == session)
        val summary = link.summary()
        if (shared == null || summary != null && (summary.atlasId to summary.game) != shared) reply("ended")
        else if (summary == null || summary.stateWire == null || unavailable()) reply("unavailable")
        else {
            reseat(summary)
            when (request.text("op")) {
                "preview", "join" -> {
                    if (failures.size >= MAX_FAILURES && now() - failures.first() <= 60_000) reply("locked")
                    else if (!keyAccepted(request.text("key"))) reply(if (failures.size >= MAX_FAILURES) "locked" else "unauthorized")
                    else if (request.text("op") == "preview") snapshot("ok", summary)
                    else join(request, summary)
                }
                else -> {
                    val token = request.text("token")
                    val client = clients[token]
                    if (client == null) reply(if (released.remove(token)) "released" else "unauthorized")
                    else {
                        client.seen = now()
                        when (request.text("op")) {
                            "poll" -> snapshot("ok", summary).put("player", client.guest.player)
                            "leave" -> { clients.remove(client.guest.token); reply("ok") }
                            "command" -> command(client, request, summary)
                            else -> reply("invalid")
                        }
                    }
                }
            }
        }
    } catch (e: kotlinx.coroutines.CancellationException) { throw e } catch (_: Exception) { reply("invalid") }

    private fun join(request: JSONObject, summary: TableSummary): JSONObject {
        val id = request.text("joinId")
        require(id.matches(Regex("[0-9a-f-]{36}")))
        val role = request.text("role")
        val player = request.integer("player").also { require(it in 0..16) }.toInt()
        val seat = summary.players.firstOrNull { it.playerNumber == player }
        require(role == "player" && seat != null || role == "display" && player == 0)
        val name = seat?.label
        clients.entries.removeAll { now() - it.value.seen > 5 * 60_000 }
        val repeated = clients.values.firstOrNull { it.joinId == id }
        if (repeated != null) {
            require(repeated.guest.role == role && repeated.guest.name == name)
            repeated.seen = now()
            return snapshot("ok", summary).put("token", repeated.guest.token).put("player", repeated.guest.player)
        }
        if (clients.size >= 16) return reply("full")
        if (player != 0 && clients.values.any { it.guest.player == player }) return snapshot("claimed", summary)
        val token = secret()
        clients[token] = Client(LanGuest(token, role, player, name), id, now())
        return snapshot("ok", summary).put("token", token).put("player", player)
    }

    private suspend fun command(client: Client, request: JSONObject, summary: TableSummary): JSONObject {
        if (client.guest.role != "player") return reply("forbidden")
        val id = request.text("actionId")
        require(id.matches(Regex("[0-9a-f-]{36}")))
        val kind = request.text("kind")
        require(kind.length <= 64)
        val value = request.integer("value").also { require(it in -100_000..100_000) }.toInt()
        val slot = request.integer("slot").also { require(it in 0..10) }.toInt()
        val source = if (request.has("source")) request.integer("source").also { require(it in 0..16) }.toInt() else 0
        val match = request.text("match")
        require(match.length in 1..128)
        val payload = listOf(match, kind, value, slot, source).joinToString("|")
        client.receipts[id]?.let { return if (it.payload == payload) snapshot(it.code, summary).put("player", client.guest.player) else reply("invalid") }
        if (client.receipts.size >= 2048) return snapshot("renew", summary)
        // Atlas orders every tap itself, so only another boot or game makes one stale, not a newer revision.
        val code = when {
            match != summary.relayMatch() -> "stale"
            !link.canAct() -> "view-only"
            else -> {
                val action = atlasRelayAction(summary, client.guest.player, kind, value, slot, source)
                if (action != null && link.act(summary.game, action)) "ok" else "rejected"
            }
        }
        // A tap Atlas never saw may be sent again; only its decisions are kept.
        if (code == "ok" || code == "rejected") client.receipts[id] = Receipt(payload, code)
        return snapshot(code, link.summary() ?: summary).put("player", client.guest.player)
    }

    private fun snapshot(code: String, summary: TableSummary): JSONObject {
        val frame = LanAtlasFrame.encode(summary, clock()) ?: return reply("unavailable")
        return reply(code).put("atlas", frame)
            .put("claimed", JSONArray(clients.values.map { it.guest.player }.filter { it > 0 }))
            .put("viewOnly", !link.canAct())
    }

    private fun reply(code: String) = JSONObject().put("v", LAN_VERSION).put("session", session).put("code", code)

    private companion object {
        const val MAX_FAILURES = 10
    }
}
