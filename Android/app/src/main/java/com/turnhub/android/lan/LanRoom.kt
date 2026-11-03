package com.turnhub.android.lan

import org.json.JSONArray
import org.json.JSONObject

/** Room membership is independent of seats, matches and presentation. Only the hub mutates it. */
internal class LanRoom(
    private val record: JSONObject,
    private val save: (JSONObject) -> Boolean,
    private val snapshot: suspend () -> JSONObject,
    private val seats: () -> List<RoomSeat>,
    private val profiles: suspend () -> JSONArray,
    private val attach: suspend (JSONObject) -> String?,
    private val command: suspend (JSONObject, Int) -> JSONObject,
    private val now: () -> Long = { System.nanoTime() / 1_000_000 },
) {
    data class RoomSeat(val identity: String, val number: Int, val name: String)
    private val epoch = randomLanSecret(16)
    private val members = record.optJSONArray("members") ?: JSONArray().also { record.put("members", it) }
    private val receipts = mutableMapOf<String, LinkedHashMap<String, Pair<String, JSONObject>>>()
    private val failures = ArrayDeque<Long>()
    val guests: List<LanGuest> get() = (0 until members.length()).map { i ->
        val m = members.getJSONObject(i)
        LanGuest(m.text("token"), m.text("role"), seat(m), m.optString("name"), m.optBoolean("master"))
    }
    private fun seat(m: JSONObject) = seats().firstOrNull { it.identity == m.optString("identity") }?.number ?: 0
    private fun member(token: String) = (0 until members.length()).map { members.getJSONObject(it) }.firstOrNull { sameSecret(it.text("token"), token) }
    private fun persist() {
        val old = record.optLong("sequence")
        record.put("sequence", old + 1)
        if (!save(record)) { record.put("sequence", old); error("Room could not be saved") }
    }
    fun revoke(token: String) {
        val i = (0 until members.length()).firstOrNull { sameSecret(members.getJSONObject(it).text("token"), token) } ?: return
        val old = members.remove(i)
        try { persist(); receipts.remove(token) } catch (e: Exception) { members.put(old); throw e }
    }
    fun grant(token: String, master: Boolean) {
        val m = member(token) ?: return
        val old = m.optBoolean("master")
        m.put("master", master)
        try { persist() } catch (e: Exception) { m.put("master", old); throw e }
    }
    private fun reply(code: String) = JSONObject().put("v", LAN_VERSION).put("session", record.text("session")).put("code", code).put("epoch", epoch)
    private fun memberEpoch(m: JSONObject) = "$epoch/${m.optLong("commandGeneration")}"
    private suspend fun frame(code: String, m: JSONObject? = null): JSONObject {
        val f = snapshot()
        if (f.optString("code") != "ok") return f.put("epoch", epoch)
        f.put("code", code).put("epoch", epoch)
        f.put("directory", profiles())
        if (m != null) f.put("player", seat(m)).put("role", m.text("role")).put("master", m.optBoolean("master")).put("epoch", memberEpoch(m))
        return f
    }
    suspend fun handle(r: JSONObject): JSONObject {
        val result = dispatch(r)
        if (result.has("looksId") && result.optString("looksId") == r.optString("looksId")) result.remove("looks")
        return result
    }
    private suspend fun dispatch(r: JSONObject): JSONObject = try {
        require(r.integer("v") == LAN_VERSION.toLong() && r.text("session") == record.text("session"))
        when (r.text("op")) {
            "preview", "join" -> {
                while (failures.isNotEmpty() && now() - failures.first() > 60_000) failures.removeFirst()
                if (failures.size >= 10) reply("locked")
                else if (!sameSecret(r.text("key"), record.text("key"))) { failures.addLast(now()); reply("unauthorized") }
                else if (r.text("op") == "preview") frame("ok")
                else join(r)
            }
            else -> {
                val m = member(r.text("token"))
                if (m == null) reply("unauthorized") else when (r.text("op")) {
                    "poll" -> frame("ok", m)
                    "leave" -> { revoke(m.text("token")); reply("ok") }
                    "command" -> act(r, m)
                    else -> reply("invalid")
                }
            }
        }
    } catch (e: kotlinx.coroutines.CancellationException) { throw e }
      catch (_: IllegalArgumentException) { reply("invalid") }
      catch (_: Exception) { reply("unavailable") }

    private suspend fun join(r: JSONObject): JSONObject {
        val id = r.text("joinId").also { require(it.matches(Regex("[0-9a-f-]{36}"))) }
        (0 until members.length()).map { members.getJSONObject(it) }.firstOrNull { it.text("joinId") == id }?.let {
            return frame("ok", it).put("token", it.text("token"))
        }
        if (members.length() >= 16) return reply("full")
        val role = r.text("role").also { require(it in setOf("player", "investigator", "display", "table", "planes", "elder")) }
        val identity = if (role in setOf("player", "investigator")) {
            // PIN/profile authorization is performed once here, never on poll/reconnect.
            attach(r) ?: run { failures.addLast(now()); return reply("pin-required") }
        } else ""
        val directory = profiles()
        val name = (0 until directory.length()).map { directory.getJSONObject(it) }
            .firstOrNull { it.optString("profileId") == identity }?.optString("name").orEmpty()
        val m = JSONObject().put("token", randomLanSecret()).put("joinId", id).put("role", role)
            .put("identity", identity).put("name", seats().firstOrNull { it.identity == identity }?.name ?: name)
            .put("master", false)
        members.put(m)
        try { persist() } catch (e: Exception) { members.remove(members.length() - 1); throw e }
        return frame("ok", m).put("token", m.text("token")).put("profileLink", r.optJSONObject("_profileLink")).put("profile", r.optJSONObject("_profile"))
    }
    private suspend fun act(r: JSONObject, m: JSONObject): JSONObject {
        // A process restart loses in-memory receipts. Old commands must never be replayed.
        if (r.optString("epoch") != memberEpoch(m)) return frame("interrupted", m)
        val kind = r.text("kind")
        val privileged = m.optBoolean("master")
        val table = m.text("role") == "table"
        val own = seat(m)
        val target = if (table || privileged) r.optInt("target", own) else own
        if (kind.startsWith(PLANAR_KIND)) {
            if (!privileged) return frame("forbidden", m)
        } else if (target <= 0 || !table && !privileged && m.text("role") !in setOf("player", "investigator")) return frame("forbidden", m)
        if (seats().none { it.number == target } && !kind.startsWith(PLANAR_KIND)) return frame("stale", m)
        val id = r.text("actionId").also { require(it.matches(Regex("[0-9a-f-]{36}"))) }
        val payload = listOf(r.text("match"), r.integer("revision"), kind, r.integer("value"), r.integer("slot"), r.optInt("source"), target).joinToString("|")
        val ledger = receipts.getOrPut(m.text("token")) { linkedMapOf() }
        ledger[id]?.let { return if (it.first == payload) frame(it.second.text("code"), m) else reply("invalid") }
        if (ledger.size >= 2048) {
            val generation = m.optLong("commandGeneration")
            m.put("commandGeneration", generation + 1)
            try { persist() } catch (e: Exception) { m.put("commandGeneration", generation); throw e }
            receipts.remove(m.text("token"))
            return frame("renew", m)
        }
        val result = command(r, target)
        val code = result.text("code")
        if (code != "unavailable") ledger[id] = payload to result
        return frame(code, m)
    }
}
