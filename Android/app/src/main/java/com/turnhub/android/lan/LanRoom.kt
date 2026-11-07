package com.turnhub.android.lan

import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
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
    private val resolveIdentity: (String) -> String = { it },
    private val planesAvailable: () -> Boolean = { false },
    private val planarPlayerAllowed: (Int) -> Boolean = { false },
    private val planarRollAllowed: (Int) -> Boolean = { false },
    /** A seated player's games exchange (plans/profile-sync.md): their profile ID and the request; null is unavailable. */
    private val history: suspend (String, JSONObject) -> JSONObject? = { _, _ -> null },
    private val atlasTicket: suspend (String) -> String? = { null },
    private val atlasMember: suspend (String) -> Boolean = { false },
    private val attachAtlas: suspend (JSONObject) -> String? = { null },
    private val setup: suspend (JSONObject) -> String = { "forbidden" },
    private val setupOptions: () -> JSONObject = { JSONObject() },
    private val hostOffer: (String) -> String? = { null },
    private val acceptHost: suspend (String, String) -> JSONObject? = { _, _ -> null },
    private val hostReceived: suspend (String, String) -> Boolean = { _, _ -> false },
    private val hostReleased: () -> Boolean = { false },
) {
    data class RoomSeat(val identity: String, val number: Int, val name: String)
    private val epoch = randomLanSecret(16)
    private val members = record.optJSONArray("members") ?: JSONArray().also { record.put("members", it) }
    private data class Receipt(val payload: String, val code: String, val roll: JSONObject?)
    private val receipts = mutableMapOf<String, LinkedHashMap<String, Receipt>>()
    private val commandLock = Mutex()
    private val failures = ArrayDeque<Long>()
    val guests: List<LanGuest> get() = (0 until members.length()).map { i ->
        val m = members.getJSONObject(i)
        LanGuest(m.text("token"), m.text("role"), seat(m), m.optString("name"), m.optBoolean("master"),
            m.optString("deviceName"), m.optBoolean("hostHandoff"))
    }
    private fun claimed(identity: String) = (0 until members.length()).map { members.getJSONObject(it) }
        .any { it.text("role") == "player" && resolveIdentity(it.optString("identity")) == resolveIdentity(identity) }
    private fun joinKeyHash(key: String) = java.security.MessageDigest.getInstance("SHA-256").digest(key.toByteArray()).hex()
    private fun controllerOwner(key: String) = (0 until members.length()).map { members.getJSONObject(it) }
        .firstOrNull { it.text("role") == "player" && it.optString("controllerKey").isNotEmpty() && sameSecret(it.optString("controllerKey"), key) }
    private fun claimedProfiles() = JSONArray((0 until members.length()).map { members.getJSONObject(it) }
        .filter { it.text("role") == "player" }.map { resolveIdentity(it.optString("identity")) }.distinct())
    private suspend fun availableProfiles(): JSONArray {
        val all = profiles()
        return JSONArray((0 until all.length()).map { all.getJSONObject(it) }.filterNot { claimed(it.optString("profileId")) })
    }
    private fun seat(m: JSONObject) = seats().firstOrNull { it.identity == resolveIdentity(m.optString("identity")) }?.number ?: 0
    private fun member(token: String) = (0 until members.length()).map { members.getJSONObject(it) }.firstOrNull { sameSecret(it.text("token"), token) }
    private fun persist() {
        val old = record.optLong("sequence")
        record.put("sequence", old + 1)
        if (!save(record)) { record.put("sequence", old); error("Room could not be saved") }
    }
    private val orientations = record.optJSONObject("displayOrientations") ?: JSONObject().also { record.put("displayOrientations", it) }
    private val views = record.optJSONObject("displayViews") ?: JSONObject().also { record.put("displayViews", it) }
    private fun displayMember(target: String) = (0 until members.length()).map { members.getJSONObject(it) }
        .firstOrNull { it.text("joinId") == target && it.text("role") in setOf("display", "table", "planes") }
    fun displayView(target: String): String? = displayMember(target)?.let {
        views.optString(target, if (it.text("role") == "planes") "planes" else "table")
    }
    fun setDisplayView(target: String, view: String) {
        require(displayMember(target) != null && view in setOf("table", "planes"))
        require(view != "planes" || planesAvailable())
        if (displayView(target) == view) return
        val old = views.opt(target)
        views.put(target, view)
        try { persist() } catch (e: Exception) {
            if (old == null) views.remove(target) else views.put(target, old)
            throw e
        }
    }
    fun displayAllUpright(target: String): Boolean = orientations.optBoolean(target)
    fun displayTargets(): JSONArray = JSONArray().put(JSONObject().put("id", "host")
        .put("name", "Host screen").put("detail", friendlyDisplayDevice(record.optString("hostDeviceName")))
        .put("public", true).put("upright", displayAllUpright("host"))).also { targets ->
        (0 until members.length()).forEach { i ->
            val m = members.getJSONObject(i)
            val id = m.text("joinId")
            val role = m.text("role")
            val name = when (role) {
                "display", "table" -> "Public table display"
                "planes" -> "Planes display"
                else -> m.optString("name").takeIf { it.isNotBlank() }?.let { "$it's screen" } ?: "Player screen"
            }
            targets.put(JSONObject().put("id", id).put("name", name)
                .put("detail", friendlyDisplayDevice(m.optString("deviceName")))
                .put("public", role in setOf("display", "table", "planes"))
                .put("view", displayView(id))
                .put("upright", displayAllUpright(id)))
        }
    }
    fun setDisplayAllUpright(target: String, value: Boolean) {
        require(target == "host" || (0 until members.length()).any { members.getJSONObject(it).text("joinId") == target })
        val old = displayAllUpright(target)
        if (old == value) return
        orientations.put(target, value)
        try { persist() } catch (e: Exception) { orientations.put(target, old); throw e }
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
        f.put("setupOptions", setupOptions())
        f.put("directory", availableProfiles()).put("claimedProfiles", claimedProfiles()).put("displayTargets", displayTargets())
            .put("displayId", m?.text("joinId") ?: "host")
            .put("displayAllUpright", displayAllUpright(m?.text("joinId") ?: "host"))
            .put("displayView", displayView(m?.text("joinId") ?: "host"))
        if (m != null) {
            f.put("player", seat(m)).put("role", m.text("role")).put("master", m.optBoolean("master")).put("epoch", memberEpoch(m))
            hostOffer(m.text("token"))?.let { f.put("hostOffer", it) }
        }
        if (hostReleased()) f.put("code", "ended")
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
                else {
                    val owner = controllerOwner(r.text("key"))
                    val scope = owner?.let { resolveIdentity(it.optString("identity")) }
                    val atlasPhone = if (scope == null && !sameSecret(r.text("key"),record.text("key"))) atlasTicket(r.text("key")) else null
                    if (scope == null && atlasPhone == null && !sameSecret(r.text("key"), record.text("key")) ||
                        scope != null && r.optString("profileOnly") != scope ||
                        scope == null && r.has("profileOnly")) {
                        failures.addLast(now()); reply("unauthorized")
                    } else if (r.text("op") == "preview") frame("ok").also {
                        if (scope != null) it.put("profileOnly", scope)
                        if (atlasPhone != null) it.put("directory",profiles()).put("claimedProfiles",JSONArray())
                    }
                    else commandLock.withLock { join(r, scope, atlasPhone) }
                }
            }
            else -> {
                val m = member(r.text("token"))
                // After relinquishing, a selected member can retry collection/receipt even
                // though Atlas has released its old controller ticket. No new gameplay is allowed.
                val collecting = r.text("op") in setOf("accept-host", "host-received") && hostOffer(r.text("token")) == r.optString("transfer")
                if (m == null || (!(collecting && hostReleased()) && m.optString("atlasPhone").isNotEmpty() && !atlasMember(m.optString("atlasPhone")))) reply("unauthorized") else when (r.text("op")) {
                    "accept-host" -> commandLock.withLock {
                        if (hostOffer(m.text("token")) != r.text("transfer")) reply("cancelled")
                        else acceptHost(m.text("token"), r.text("transfer"))?.let { reply("ok").put("game", it) } ?: reply("unavailable")
                    }
                    "host-received" -> commandLock.withLock {
                        reply(if (hostReceived(m.text("token"), r.text("transfer"))) "ok" else "forbidden")
                    }
                    "poll" -> {
                        // Backfill model labels for memberships created before display targeting.
                        val device = r.optString("deviceName").take(64)
                        if (device.isNotBlank() && device != m.optString("deviceName") || r.optBoolean("hostHandoff") && !m.optBoolean("hostHandoff")) {
                            val old = m.optString("deviceName")
                            val oldCapability = m.optBoolean("hostHandoff")
                            if (device.isNotBlank()) m.put("deviceName", device)
                            m.put("hostHandoff", r.optBoolean("hostHandoff") || oldCapability)
                            try { persist() } catch (e: Exception) { m.put("deviceName", old).put("hostHandoff", oldCapability); throw e }
                        }
                        frame("ok", m)
                    }
                    "presentation" -> commandLock.withLock {
                        if (member(m.text("token")) == null) reply("unauthorized")
                        else if (m.text("role") !in setOf("player", "table") && !m.optBoolean("master") &&
                            !(r.has("displayView") && !r.has("displayAllUpright") && r.optString("displayTarget") == m.text("joinId") &&
                                m.text("role") in setOf("display", "planes"))) frame("forbidden", m)
                        else {
                            require(r.has("displayView") != r.has("displayAllUpright"))
                            if (r.has("displayView")) {
                                val view = r.get("displayView").also { require(it is String) } as String
                                setDisplayView(r.text("displayTarget"), view)
                            }
                            else {
                                val upright = r.get("displayAllUpright").also { require(it is Boolean) } as Boolean
                                setDisplayAllUpright(r.text("displayTarget"), upright)
                            }
                            frame("ok", m)
                        }
                    }
                    "invite-controller" -> commandLock.withLock {
                        if (member(m.text("token")) == null) reply("unauthorized")
                        else if (m.text("role") != "player" || m.optString("atlasPhone").isNotEmpty() || !m.optString("identity").matches(Regex("[0-9A-F]{8}"))) reply("forbidden")
                        else {
                            val old = m.optString("controllerKey")
                            val key = old.ifEmpty { randomLanSecret() }
                            m.put("controllerKey", key)
                            try { if (old.isEmpty()) persist() } catch (e: Exception) { m.remove("controllerKey"); throw e }
                            reply("ok").put("controllerKey", key).put("profileOnly", resolveIdentity(m.text("identity")))
                        }
                    }
                    "leave" -> { revoke(m.text("token")); reply("ok") }
                    "command" -> commandLock.withLock { if (member(m.text("token")) == null) reply("unauthorized") else act(r, m) }
                    "history" -> commandLock.withLock {
                        val identity = resolveIdentity(m.optString("identity"))
                        if (member(m.text("token")) == null) reply("unauthorized")
                        else if (m.text("role") != "player" || identity.isBlank() || m.optString("atlasPhone").isNotEmpty()) reply("forbidden")
                        else history(identity, r)?.let { extra ->
                            reply("ok").also { out -> extra.keys().forEach { key -> out.put(key, extra.get(key)) } }
                        } ?: reply("unavailable")
                    }
                    else -> reply("invalid")
                }
            }
        }
    } catch (e: kotlinx.coroutines.CancellationException) { throw e }
      catch (_: IllegalArgumentException) { reply("invalid") }
      catch (_: Exception) { reply("unavailable") }

    private suspend fun join(r: JSONObject, scope: String? = null, atlasPhone: String? = null): JSONObject {
        listOf("_profileLink", "_profile", "_attachError", "_atlasId").forEach(r::remove)
        val id = r.text("joinId").also { require(it.matches(Regex("[0-9a-f-]{36}"))) }
        (0 until members.length()).map { members.getJSONObject(it) }.firstOrNull { it.text("joinId") == id }?.let {
            if (it.optString("joinKeyHash") != joinKeyHash(r.text("key"))) return reply("unauthorized")
            return frame("ok", it).put("token", it.text("token"))
        }
        if (members.length() >= 16 && atlasPhone==null) return reply("full")
        val role = if (scope != null) "player" else r.text("role").also { require(it in setOf("player", "display", "table", "planes")) }
        val requested = r.optString("profileId").ifBlank { r.optJSONObject("profile")?.optString("profileId").orEmpty() }
        if (atlasPhone == null && scope == null && role == "player" && requested.isNotEmpty() && claimed(requested)) return reply("claimed")
        val identity = if (scope != null) scope else if (role == "player") {
            (if(atlasPhone != null) attachAtlas(r) else attach(r)) ?: run { failures.addLast(now()); return reply(r.optString("_attachError", "pin-required")) }
        } else ""
        if (atlasPhone == null && scope == null && role == "player" && claimed(identity)) return reply("claimed")
        if(atlasPhone!=null) (0 until members.length()).map { members.getJSONObject(it) }
            .firstOrNull { it.optString("atlasPhone")==atlasPhone && it.text("role")==role && it.optString("identity")==identity }
            ?.let { return frame("ok",it).put("token",it.text("token")) }
        val directory = profiles()
        // Profile verification/directory reads may suspend. Recheck membership after them.
        (0 until members.length()).map { members.getJSONObject(it) }.firstOrNull { it.text("joinId") == id }?.let {
            if (it.optString("joinKeyHash") != joinKeyHash(r.text("key"))) return reply("unauthorized")
            return frame("ok", it).put("token", it.text("token"))
        }
        if (members.length() >= 16) return reply("full")
        val name = (0 until directory.length()).map { directory.getJSONObject(it) }
            .firstOrNull { it.optString("profileId") == identity }?.optString("name").orEmpty()
        val m = JSONObject().put("token", randomLanSecret()).put("joinId", id).put("role", role)
            .put("atlasPhone",atlasPhone ?: "")
            .put("identity", identity).put("name", seats().firstOrNull { it.identity == identity }?.name ?: name)
            .put("master", false).put("joinKeyHash", joinKeyHash(r.text("key"))).put("deviceName", r.optString("deviceName").take(64))
            .put("hostHandoff", r.optBoolean("hostHandoff"))
        members.put(m)
        try { persist() } catch (e: Exception) { members.remove(members.length() - 1); throw e }
        return frame("ok", m).put("token", m.text("token")).put("profileLink", r.optJSONObject("_profileLink")).put("profile", r.optJSONObject("_profile"))
    }
    private suspend fun act(r: JSONObject, m: JSONObject): JSONObject {
        // A process restart loses in-memory receipts. Old commands must never be replayed.
        if (r.optString("epoch") != memberEpoch(m)) return frame("interrupted", m)
        val kind = r.text("kind").also { require(it.length in 1..64) }
        require(r.text("match").length <= 128)
        val privileged = m.optBoolean("master")
        val table = m.text("role") == "table"
        val own = seat(m)
        val target = if (table || privileged) r.optInt("target", own) else own
        val id = r.text("actionId").also { require(it.matches(Regex("[0-9a-f-]{36}"))) }
        val card = LanCard.read(r)
        val payload = listOf(card?.json(), r.text("match"), r.integer("revision"), kind, r.integer("value"), r.integer("slot"), r.optInt("source"), if (kind == "setup") 0 else target, r.optJSONObject("setup")?.toString()).joinToString("|")
        val ledger = receipts.getOrPut(m.text("token")) { linkedMapOf() }
        ledger[id]?.let { return if (it.payload == payload) frame(it.code, m).put("rollResult", it.roll) else reply("invalid") }
        if (kind == "setup") {
            if (m.text("role") !in setOf("player", "table") && !privileged) return frame("forbidden", m)
            require(r.integer("value") == 0L && r.integer("slot") == 0L && r.optInt("source") == 0)
            require(r.getJSONObject("setup").toString().length <= 16384)
        } else if (kind.startsWith(PLANAR_KIND)) {
            if (m.text("role") == "player" && isPlanarRollKind(kind) && !planarRollAllowed(own)) return frame("forbidden", m)
            if (!privileged && !(planesAvailable() && (table ||
                    m.text("role") == "player" && own > 0 && planarPlayerAllowed(own)))) return frame("forbidden", m)
        } else if (target <= 0 || !table && !privileged && m.text("role") != "player") return frame("forbidden", m)
        if (seats().none { it.number == target } && !kind.startsWith(PLANAR_KIND) && kind != "setup") return frame("stale", m)
        if (ledger.size >= 2048 || ledger.values.sumOf { it.payload.length } + payload.length > 512 * 1024) {
            val generation = m.optLong("commandGeneration")
            m.put("commandGeneration", generation + 1)
            try { persist() } catch (e: Exception) { m.put("commandGeneration", generation); throw e }
            receipts.remove(m.text("token"))
            return frame("renew", m)
        }
        val result = if (kind == "setup") JSONObject().put("code",setup(r)) else command(r, target)
        val code = result.text("code")
        val roll = recordedPlanarRoll(kind, result)
        if (code != "unavailable") ledger[id] = Receipt(payload, code, roll)
        return frame(code, m).put("rollResult", roll)
    }
}
