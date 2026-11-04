package com.turnhub.android.lan

import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.async
import kotlinx.coroutines.awaitAll
import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test
import java.util.UUID

class LanRoomTest {
    private class Fixture(saved: JSONObject? = null) {
        var record = saved ?: JSONObject().put("session", "room").put("key", "abc-123")
        var roster = listOf(LanRoom.RoomSeat("PROFILE1", 1, "Sam"), LanRoom.RoomSeat("PROFILE2", 2, "Sam"))
        var match = "first"
        var revision = 0L
        var changes = 0
        var lastTarget = 0
        var delayCommand = false
        var delayAttach = false
        var writable = true
        var authorized = true
        var planesAvailable = true
        var alias: Pair<String, String>? = null
        val room = LanRoom(record, planesAvailable = { planesAvailable }, save = { if (writable) { record = JSONObject(it.toString()); true } else false },
            resolveIdentity = { id -> if (id == alias?.first) alias!!.second else id },
            snapshot = { base("ok").put("code", "ok").put("match", match) }, seats = { roster },
            profiles = { JSONArray() }, attach = { if (delayAttach) kotlinx.coroutines.delay(10)
                if (authorized && it.optString("pin") == "1234") it.optString("profileId") else null },
            command = { r, target ->
                lastTarget = target
                if (delayCommand) kotlinx.coroutines.delay(10)
                if (r.text("match") != match || r.integer("revision") != revision) base("stale").put("code", "stale")
                else { changes++; revision++; base("ok").put("code", "ok") }
            })
        fun base(op: String) = JSONObject().put("v", LAN_VERSION).put("session", "room").put("op", op)
        suspend fun join(role: String = "player", profile: String = "PROFILE1", pin: String = "1234", id: String = UUID.randomUUID().toString()) =
            room.handle(base("join").put("key", "abc-123").put("role", role).put("profileId", profile).put("pin", pin).put("joinId", id))
        fun poll(token: String) = base("poll").put("token", token)
        fun action(joined: JSONObject, kind: String = "life", target: Int = 1) = base("command")
            .put("token", joined.text("token")).put("epoch", joined.text("epoch")).put("actionId", UUID.randomUUID().toString())
            .put("match", match).put("revision", revision).put("kind", kind).put("value", -1).put("slot", 0).put("target", target)
    }
    @Test fun `players target one durable display without changing game or other screens`() = runBlocking {
        val f = Fixture()
        val player = f.join()
        val display = f.join("display")
        val target = display.text("displayId")
        val publicTarget = (0 until display.getJSONArray("displayTargets").length()).map { display.getJSONArray("displayTargets").getJSONObject(it) }
            .single { it.text("id") == target }
        assertEquals("Public table display", publicTarget.text("name"))
        assertTrue(publicTarget.getBoolean("public"))
        fun presentation(joined: JSONObject, id: String, value: Any) = f.base("presentation").put("token", joined.text("token"))
            .put("displayTarget", id).put("displayAllUpright", value)
        assertFalse(player.getBoolean("displayAllUpright"))
        assertEquals("ok", f.room.handle(presentation(player, target, true)).text("code"))
        assertTrue(f.room.handle(f.poll(display.text("token"))).getBoolean("displayAllUpright"))
        assertFalse(f.room.handle(f.poll(player.text("token"))).getBoolean("displayAllUpright"))
        assertFalse(f.room.displayAllUpright("host"))
        val sequence = f.record.getLong("sequence")
        assertEquals("ok", f.room.handle(presentation(player, target, true)).text("code"))
        assertEquals(sequence, f.record.getLong("sequence"))
        assertEquals(0, f.changes)
        assertFalse(f.room.guests.first().master)
        val restored = Fixture(JSONObject(f.record.toString()))
        assertTrue(restored.room.handle(restored.poll(display.text("token"))).getBoolean("displayAllUpright"))
        assertEquals("forbidden", f.room.handle(presentation(display, target, false)).text("code"))
        assertEquals("unauthorized", f.room.handle(presentation(JSONObject().put("token", "unknown"), target, false)).text("code"))
        assertNotEquals("ok", f.room.handle(presentation(player, target, "true")).text("code"))
        assertNotEquals("ok", f.room.handle(presentation(player, "missing", true)).text("code"))
        f.writable = false
        assertEquals("unavailable", f.room.handle(presentation(player, target, false)).text("code"))
        assertTrue(f.room.displayAllUpright(target))
        f.writable = true
        assertEquals("ok", f.room.handle(presentation(player, target, false)).text("code"))
        assertFalse(f.room.displayAllUpright(target))
    }

    @Test fun `selected public content survives restart without changing seats roles or other screens`() = runBlocking {
        val f = Fixture()
        val player = f.join()
        val screen = f.join("display")
        val other = f.join("planes")
        val target = screen.text("displayId")
        fun change(view: String) = f.base("presentation").put("token", player.text("token"))
            .put("displayTarget", target).put("displayView", view)
        assertEquals("table", screen.text("displayView"))
        assertEquals("ok", f.room.handle(change("planes")).text("code"))
        assertEquals("planes", f.room.handle(f.poll(screen.text("token"))).text("displayView"))
        assertEquals("planes", f.room.handle(f.poll(other.text("token"))).text("displayView"))
        val sequence = f.record.getLong("sequence")
        assertEquals("ok", f.room.handle(change("planes")).text("code"))
        assertEquals(sequence, f.record.getLong("sequence"))
        val restored = Fixture(JSONObject(f.record.toString()))
        assertEquals("planes", restored.room.handle(restored.poll(screen.text("token"))).text("displayView"))
        assertEquals("ok", f.room.handle(change("table")).text("code"))
        assertEquals("planes", f.room.displayView(other.text("displayId")))
        assertEquals(0, f.changes)
        assertFalse(f.room.guests.any { it.master })
        assertEquals("display", f.room.guests[1].role)
        assertFalse(f.room.displayAllUpright(target))
    }

    @Test fun `public content validates availability target value and durable write`() = runBlocking {
        val f = Fixture()
        val player = f.join()
        val screen = f.join("display")
        val target = screen.text("displayId")
        fun change(id: String = target, view: String = "planes") = f.base("presentation")
            .put("token", player.text("token")).put("displayTarget", id).put("displayView", view)
        assertEquals("invalid", f.room.handle(change(view = "personal")).text("code"))
        assertEquals("invalid", f.room.handle(change().put("displayView", 1)).text("code"))
        assertEquals("invalid", f.room.handle(change(id = "missing")).text("code"))
        assertEquals("invalid", f.room.handle(change(id = player.text("displayId"))).text("code"))
        assertEquals("invalid", f.room.handle(change().put("displayAllUpright", true)).text("code"))
        f.planesAvailable = false
        assertEquals("invalid", f.room.handle(change()).text("code"))
        f.planesAvailable = true
        f.writable = false
        assertEquals("unavailable", f.room.handle(change()).text("code"))
        assertEquals("table", f.room.displayView(target))
        f.writable = true
        assertEquals("ok", f.room.handle(change()).text("code"))
        f.planesAvailable = false
        assertEquals("ok", f.room.handle(change(view = "table")).text("code"))
    }

    @Test fun `spectator can switch itself but never another display or its orientation`() = runBlocking {
        val f = Fixture()
        val screen = f.join("display")
        val other = f.join("planes")
        fun change(target: String) = f.base("presentation").put("token", screen.text("token"))
            .put("displayTarget", target).put("displayView", "planes")
        assertEquals("ok", f.room.handle(change(screen.text("displayId"))).text("code"))
        assertEquals("forbidden", f.room.handle(change(other.text("displayId"))).text("code"))
        assertEquals("forbidden", f.room.handle(change(screen.text("displayId")).put("displayAllUpright", true)).text("code"))
        assertEquals("unauthorized", f.room.handle(change(screen.text("displayId")).put("token", "bad")).text("code"))
        assertEquals("forbidden", f.room.handle(f.action(screen)).text("code"))
    }

    @Test fun `PIN is needed on first link but never on reconnect`() = runBlocking {
        val f = Fixture()
        assertEquals("pin-required", f.join(pin = "wrong").text("code"))
        val joined = f.join()
        f.authorized = false
        val restored = Fixture(JSONObject(f.record.toString()))
        restored.authorized = false
        assertEquals("ok", restored.room.handle(restored.poll(joined.text("token"))).text("code"))
    }
    @Test fun `same names bind by identity and members stay when sitting out or changing games`() = runBlocking {
        val f = Fixture()
        val joined = f.join(profile = "PROFILE2")
        val token = joined.text("token")
        f.roster = listOf(LanRoom.RoomSeat("PROFILE2", 1, "Renamed"))
        assertEquals(1, f.room.handle(f.poll(token)).getInt("player"))
        f.roster = emptyList()
        f.match = "pokemon"
        assertEquals(0, f.room.handle(f.poll(token)).getInt("player"))
        f.roster = listOf(LanRoom.RoomSeat("PROFILE2", 2, "Renamed"))
        assertEquals(2, f.room.handle(f.poll(token)).getInt("player"))
    }
    @Test fun `shared tablet needs no profile PIN and can act for another seat`() = runBlocking {
        val f = Fixture()
        val joined = f.join("table", pin = "")
        assertEquals("ok", joined.text("code"))
        assertEquals("ok", f.room.handle(f.action(joined, target = 2)).text("code"))
        assertEquals(1, f.changes)
    }
    @Test fun `public views and planes are read only until explicitly granted`() = runBlocking {
        val f = Fixture()
        for (role in listOf("display", "planes", "elder")) {
            val joined = f.join(role)
            assertEquals("forbidden", f.room.handle(f.action(joined)).text("code"))
            assertEquals("forbidden", f.room.handle(f.action(joined, "planar-roll")).text("code"))
            f.room.grant(joined.text("token"), true)
            assertEquals("ok", f.room.handle(f.action(joined, "planar-roll")).text("code"))
        }
    }
    @Test fun `immutable action receipts reject changed payload and repeated deltas`() = runBlocking {
        val f = Fixture()
        val joined = f.join()
        val action = f.action(joined)
        assertEquals("ok", f.room.handle(action).text("code"))
        assertEquals("ok", f.room.handle(action).text("code"))
        assertEquals("invalid", f.room.handle(JSONObject(action.toString()).put("value", -2)).text("code"))
        assertEquals(1, f.changes)
    }
    @Test fun `restoring membership cannot replay a command whose receipt was lost`() = runBlocking {
        val f = Fixture()
        val joined = f.join()
        val action = f.action(joined)
        assertEquals("ok", f.room.handle(action).text("code"))
        val rebooted = Fixture(JSONObject(f.record.toString()))
        assertEquals("interrupted", rebooted.room.handle(action).text("code"))
        assertEquals(0, rebooted.changes)
        assertEquals("ok", rebooted.room.handle(rebooted.poll(joined.text("token"))).text("code"))
    }
    @Test fun `failed membership writes never acknowledge a join or grant`() = runBlocking {
        val f = Fixture()
        val joined = f.join()
        f.writable = false
        assertEquals("unavailable", f.join("display").text("code"))
        assertEquals(1, f.room.guests.size)
        assertTrue(runCatching { f.room.grant(joined.text("token"), true) }.isFailure)
        assertFalse(f.room.guests.single().master)
    }
    @Test fun `revocation is durable and multiple devices can share a participant`() = runBlocking {
        val f = Fixture()
        val a = f.join()
        val b = f.join()
        assertEquals("ok", b.text("code"))
        f.room.revoke(a.text("token"))
        val restored = Fixture(JSONObject(f.record.toString()))
        assertEquals("unauthorized", restored.room.handle(restored.poll(a.text("token"))).text("code"))
        assertEquals("ok", restored.room.handle(restored.poll(b.text("token"))).text("code"))
    }
    @Test fun `concurrent verified joins deduplicate identity and respect room capacity`() = runBlocking {
        val f = Fixture()
        f.delayAttach = true
        val id = UUID.randomUUID().toString()
        val repeated = listOf(async { f.join(id = id) }, async { f.join(id = id) }).awaitAll()
        assertEquals(repeated[0].text("token"), repeated[1].text("token"))
        assertEquals(1, f.room.guests.size)
        repeat(14) { assertEquals("ok", f.join("display").text("code")) }
        val last = listOf(async { f.join() }, async { f.join() }).awaitAll()
        assertEquals(setOf("ok", "full"), last.map { it.text("code") }.toSet())
        assertEquals(16, f.room.guests.size)
    }
    @Test fun `concurrent retries cannot apply the same Atlas-style command twice`() = runBlocking {
        val f = Fixture()
        val joined = f.join()
        f.delayCommand = true
        val action = f.action(joined)
        val replies = listOf(async { f.room.handle(action) }, async { f.room.handle(action) }).awaitAll()
        assertTrue(replies.all { it.text("code") == "ok" })
        assertEquals(1, f.changes)
    }
    @Test fun `personal membership ignores a forged other-player target`() = runBlocking {
        val f = Fixture()
        val joined = f.join()
        assertEquals("ok", f.room.handle(f.action(joined, target = 2)).text("code"))
        assertEquals(1, f.lastTarget)
    }
    @Test fun `command generation renews without another PIN and rejects old commands`() = runBlocking {
        val f = Fixture()
        val joined = f.join()
        val first = f.action(joined)
        assertEquals("ok", f.room.handle(first).text("code"))
        repeat(2047) { assertEquals("ok", f.room.handle(f.action(joined)).text("code")) }
        val renewal = f.room.handle(f.action(joined))
        assertEquals("renew", renewal.text("code"))
        assertEquals(2048, f.changes)
        assertEquals("interrupted", f.room.handle(first).text("code"))
        joined.put("epoch", renewal.text("epoch"))
        f.authorized = false
        assertEquals("ok", f.room.handle(f.action(joined)).text("code"))
    }
    @Test fun `an explicit profile merge redirects room membership without another PIN`() = runBlocking {
        val f = Fixture()
        val joined = f.join()
        f.authorized = false
        f.alias = "PROFILE1" to "PROFILE2"
        f.roster = listOf(LanRoom.RoomSeat("PROFILE2", 2, "Survivor"))
        assertEquals(2, f.room.handle(f.poll(joined.text("token"))).getInt("player"))
        assertEquals("ok", f.room.handle(f.action(joined, target = 1)).text("code"))
        assertEquals(2, f.lastTarget)
    }
    @Test fun `retrying join id does not create another member`() = runBlocking {
        val f = Fixture()
        val id = UUID.randomUUID().toString()
        assertEquals(f.join(id = id).text("token"), f.join(id = id).text("token"))
        assertEquals(1, f.room.guests.size)
    }
}
