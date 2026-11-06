package com.turnhub.android.lan

import com.turnhub.android.standalone.*
import com.turnhub.android.protocol.*
import kotlinx.coroutines.runBlocking
import org.json.*
import org.junit.Assert.*
import org.junit.Test
import java.util.UUID

class RoomSetupTest {
    private class Fixture(store: StandaloneStore = MemoryStandaloneStore(StandaloneGame(), LocalLibrary())) {
        val table = StandaloneTable(store)
        init { table.addPlayer("First"); table.addPlayer("Second") }
        val game get() = table.state.value.game
        val room = LanRoom(JSONObject().put("session", "setup-room").put("key", "abc-123"), save = { true },
            snapshot = { base("preview").put("code", "ok").put("game", StandaloneTable.gameToJson(game)) },
            seats = { game.players.mapIndexed { i, p -> LanRoom.RoomSeat(p.localId ?: "guest:$i", i + 1, p.name) } },
            profiles = { table.roomProfiles() }, attach = { game.players.first().localId },
            command = { _, _ -> JSONObject().put("code", "forbidden") }, setup = table::applyRoomSetup)
        fun base(op: String) = JSONObject().put("v", LAN_VERSION).put("session", "setup-room").put("op", op)
        suspend fun join(role: String = "player") = room.handle(base("join").put("key", "abc-123")
            .put("joinId", UUID.randomUUID().toString()).put("role", role))
        fun command(join: JSONObject, op: String, vararg fields: Pair<String, Any?>): JSONObject {
            val data = JSONObject().put("op", op)
            fields.forEach { (k,v) -> data.put(k,v ?: JSONObject.NULL) }
            return base("command").put("token", join.getString("token")).put("epoch", join.getString("epoch"))
                .put("match", game.gameId).put("revision", game.revision).put("kind", "setup").put("value",0)
                .put("slot",0).put("source",0).put("target",0).put("actionId",UUID.randomUUID().toString()).put("setup",data)
        }
        suspend fun apply(join: JSONObject, op: String, vararg fields: Pair<String,Any?>) = room.handle(command(join,op,*fields)).getString("code")
    }

    @Test fun `joined player swaps seats configures rules and starts the host game`() = runBlocking {
        val f = Fixture(); val join = f.join()
        val identities = f.game.players.map { it.localId }
        val swap = f.command(join,"swap","a" to 0,"b" to 1)
        assertEquals("ok", f.room.handle(swap).getString("code"))
        assertEquals("ok", f.room.handle(swap).getString("code"))
        assertEquals(identities.reversed(), f.game.players.map { it.localId })
        assertEquals("ok", f.apply(join,"configure","profile" to GameProfile.MTG.wireValue,"life" to 20))
        assertEquals("ok", f.apply(join,"timer","ms" to 60_000L))
        assertEquals("ok", f.apply(join,"house-rules","on" to true))
        assertEquals("ok", f.apply(join,"configure","life" to 25))
        assertEquals("ok", f.apply(join,"planechase","on" to true))
        assertEquals("ok", f.apply(join,"planar-source","source" to "physical"))
        assertEquals("ok", f.apply(join,"start","index" to 1))
        assertEquals(TableState.RUNNING, f.game.state)
        assertEquals(25, f.game.startingLife)
        assertEquals(1, f.game.starter)
        val revision = f.game.revision
        assertEquals("rejected", f.apply(join,"clear"))
        assertEquals(revision, f.game.revision)
    }

    @Test fun `setup retries do not add guests twice and stale drags cannot reorder another snapshot`() = runBlocking {
        val f = Fixture(); val join = f.join()
        val add = f.command(join,"guest")
        assertEquals("ok", f.room.handle(add).getString("code"))
        assertEquals("ok", f.room.handle(add).getString("code"))
        assertEquals(3, f.game.players.size)
        assertTrue("Guest 1" in f.table.state.value.library.guestNames)
        assertEquals("ok", f.apply(join,"forget-guest","name" to "Guest 1"))
        assertFalse("Guest 1" in f.table.state.value.library.guestNames)
        assertEquals(3, f.game.players.size)
        val stale = f.command(join,"swap","a" to 0,"b" to 1)
        assertEquals("ok", f.apply(join,"guest"))
        val names = f.game.players.map { it.name }
        assertEquals("stale", f.room.handle(stale).getString("code"))
        assertEquals(names, f.game.players.map { it.name })
        assertEquals("invalid", f.room.handle(JSONObject(add.toString()).put("setup",JSONObject().put("op","clear"))).getString("code"))
    }

    @Test fun `setup requires authenticated controller and keeps connection when seats are cleared`() = runBlocking {
        val f = Fixture(); val player = f.join(); val display = f.join("display")
        assertEquals("forbidden", f.apply(display,"clear"))
        assertEquals("unauthorized", f.room.handle(f.command(player,"clear").put("token","wrong")).getString("code"))
        assertEquals("ok", f.apply(player,"clear"))
        assertTrue(f.game.players.isEmpty())
        assertEquals(2, f.table.state.value.library.players.size)
        assertEquals("ok", f.apply(player,"guest"))
        assertEquals(1, f.game.players.size)
        assertEquals("rejected", f.apply(player,"configure","profile" to "invalid"))
        assertEquals("rejected", f.apply(player,"remove","index" to 99))
        assertEquals("rejected", f.apply(player,"timer","ms" to "60000"))
        assertEquals("rejected", f.apply(player,"remove","index" to 0.1))
    }

    @Test fun `remote profile creation carries a verifier without granting device admin`() = runBlocking {
        val f = Fixture(); val join = f.join(); val credential = LocalCredential.create("1234")
        assertEquals("ok", f.apply(join,"create-profile","name" to "Third", "credential" to
            JSONObject().put("salt",credential.salt).put("hash",credential.hash)))
        val profile = f.table.state.value.library.players.last()
        assertTrue(profile.credential!!.verifies("1234"))
        assertNotEquals(profile.localId, f.table.state.value.library.deviceAdmin)
        assertEquals("ok", f.apply(join,"select","profileId" to profile.profileId))
        assertEquals("Third", f.game.players.last().name)
        assertEquals("rejected", f.apply(join,"create-profile","name" to "Invalid", "credential" to "not-a-verifier"))
    }
    @Test fun `failed durable setup save does not publish a new roster`() = runBlocking {
        val memory = MemoryStandaloneStore(StandaloneGame(), LocalLibrary())
        val store = object : StandaloneStore by memory {
            override fun commitSnapshot(game: String, library: String) = false
        }
        val f = Fixture(store); val join = f.join(); val before = f.game
        assertEquals("unavailable", f.apply(join,"swap","a" to 0,"b" to 1))
        assertEquals(before, f.game)
    }

    @Test fun `joined controllers can save load and delete the hosts setup without touching history`() = runBlocking {
        val f = Fixture(); val join = f.join(); val names = f.game.players.map { it.name }
        assertEquals("ok", f.apply(join,"save-setup","name" to "Friday"))
        assertEquals("ok", f.apply(join,"clear"))
        assertEquals("ok", f.apply(join,"load-setup","name" to "Friday"))
        assertEquals(names, f.game.players.map { it.name })
        assertEquals("ok", f.apply(join,"delete-setup","name" to "Friday"))
        assertTrue(f.table.state.value.library.gameSetups.isEmpty())
        assertTrue(f.table.state.value.library.history.isEmpty())
    }

    @Test fun `large setup receipts renew before exhausting the rooms memory budget`() = runBlocking {
        val f = Fixture(); val join = f.join()
        var renewed = false
        repeat(100) {
            if (!renewed) renewed = f.apply(join,"guest","padding" to "x".repeat(12_000)) == "renew"
        }
        assertTrue(renewed)
        val before = f.game
        assertEquals("interrupted",f.apply(join,"clear"))
        assertEquals(before,f.game)
    }

}
