package com.turnhub.android.lan

import kotlinx.coroutines.runBlocking
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
        var writable = true
        var authorized = true
        val room = LanRoom(record, save = { if (writable) { record = JSONObject(it.toString()); true } else false },
            snapshot = { base("ok").put("code", "ok").put("match", match) }, seats = { roster },
            profiles = { JSONArray() }, attach = { if (authorized && it.optString("pin") == "1234") it.optString("profileId") else null },
            command = { r, target ->
                lastTarget = target
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
    @Test fun `retrying join id does not create another member`() = runBlocking {
        val f = Fixture()
        val id = UUID.randomUUID().toString()
        assertEquals(f.join(id = id).text("token"), f.join(id = id).text("token"))
        assertEquals(1, f.room.guests.size)
    }
}
