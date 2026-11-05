package com.turnhub.android.standalone

import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test

class HostedSigilJoinTest {
    @Test fun `switching a Sigil is atomic and preserves both players`() {
        val table = StandaloneTable(MemoryStandaloneStore(StandaloneGame(), LocalLibrary()))
        assertTrue(table.applyHostedInput(0, action("sigil-join").put("name", "First")))
        val first=table.state.value.game.players.single()
        assertFalse(table.applyHostedInput(0, action("sigil-join").put("previousPlayer", 1).put("name", "Wrong")))
        assertEquals(first,table.state.value.game.players.single())
        assertTrue(table.applyHostedInput(0, action("sigil-join").put("previousPlayer", 0).put("name", "Second")))
        val players=table.state.value.game.players
        assertEquals(2,players.size)
        assertEquals(first.copy(sigilSeat=null),players[0])
        assertEquals("sigil:0:1",players[1].sigilSeat)
        assertTrue(table.applyHostedInput(0, action("sigil-join").put("previousPlayer",1).put("mode","attach")
            .put("playerIndex",0).put("target","").put("profileId","").put("name",first.name)))
        assertEquals(2,table.state.value.game.players.size)
        assertEquals("sigil:0:1",table.state.value.game.players[0].sigilSeat)
        assertNull(table.state.value.game.players[1].sigilSeat)
    }
    private fun action(kind: String, id: Int = 0, slot: Int = 1) = JSONObject()
        .put("kind", kind).put("sigil", id).put("slot", slot)

    @Test fun `empty lobby publishes both saved profiles and joining Ricky preserves his identity`() {
        val michael = SavedLocalPlayer("local-michael", "Michael", "ABCDEF01")
        val ricky = SavedLocalPlayer("local-ricky", "Ricky", "ABCDEF02")
        val table = StandaloneTable(MemoryStandaloneStore(StandaloneGame(),
            LocalLibrary(players = listOf(michael, ricky))))
        val state = com.turnhub.android.ui.home.hostedGameJson(table.state.value.game, table.state.value.library.players)
        assertEquals(0, state.getJSONArray("players").length())
        val profiles = state.getJSONArray("hostedProfiles")
        assertEquals(listOf("Michael", "Ricky"), (0 until profiles.length()).map { profiles.getJSONObject(it).getString("name") })
        val selected = profiles.getJSONObject(1)
        assertTrue(table.applyHostedInput(0, action("sigil-join").put("mode", "profile")
            .put("profileId", selected.getString("profileId")).put("name", selected.getString("name"))))
        val player = table.state.value.game.players.single()
        assertEquals("Ricky", player.name)
        assertEquals(ricky.localId, player.localId)
        assertEquals("sigil:0:1", player.sigilSeat)
        assertFalse(player.guest)
        assertEquals(listOf(michael, ricky), table.state.value.library.players)
    }

    @Test fun `device seats join persist publish and leave through real shared rules`() {
        val store = MemoryStandaloneStore(StandaloneGame(), LocalLibrary())
        val table = StandaloneTable(store, wallClock = { 1000 })
        assertTrue(table.applyHostedInput(0, action("sigil-join")))
        assertTrue(table.applyHostedInput(0, action("sigil-join", slot = 2)))
        assertEquals(listOf("sigil:0:1", "sigil:0:2"), table.state.value.game.players.map { it.sigilSeat })
        assertFalse(table.applyHostedInput(0, action("sigil-join")))
        val reloaded = StandaloneTable(store)
        assertEquals(2, reloaded.state.value.game.players.size)
        assertTrue(reloaded.state.value.library.players.isEmpty()) // No fake saved person/account.
        val published = StandaloneTable.gameFromJson(StandaloneTable.gameToJson(reloaded.state.value.game))
        assertEquals("sigil:0:1", published.players[0].sigilSeat)
        assertTrue(reloaded.applyHostedInput(0, action("sigil-leave", slot = 0)))
        assertEquals(2, reloaded.state.value.game.players.size)
        assertTrue(reloaded.state.value.game.players.all { it.sigilSeat == null && it.guest && it.localId == null })
    }

    @Test fun `attach preserves saved identity and detach leaves player seated`() {
        val table = StandaloneTable(MemoryStandaloneStore(StandaloneGame(), LocalLibrary()))
        table.addPlayer("Alex")
        val before = table.state.value.game.players.single()
        val attach = action("sigil-join").put("mode", "attach").put("target", before.localId)
            .put("profileId", table.state.value.library.players.single().profileId).put("name", before.name).put("playerIndex", 0)
        assertTrue(table.applyHostedInput(0, attach))
        assertEquals(before.localId, table.state.value.game.players.single().localId)
        assertFalse(table.applyHostedInput(0, attach.put("sigil", 1)))
        assertTrue(table.applyHostedInput(0, action("sigil-leave")))
        assertEquals(before, table.state.value.game.players.single())
    }

    @Test fun `saved profile join and true guest keep separate record identities`() {
        val table = StandaloneTable(MemoryStandaloneStore(StandaloneGame(), LocalLibrary()))
        assertTrue(table.applyHostedInput(0, action("sigil-join").put("mode", "profile")
            .put("profileId", "ABCDEF12").put("name", "Alex")))
        assertTrue(table.applyHostedInput(0, action("sigil-join", id = 1).put("name", "Blue Sigil")))
        val players = table.state.value.game.players
        assertEquals("ABCDEF12", table.state.value.library.players.single().profileId)
        assertNotNull(players[0].localId)
        assertTrue(players[1].guest)
        assertEquals("Blue Sigil", players[1].name)
        assertNull(players[1].localId)
        assertNull(players[1].profileId)
        val roundTrip = StandaloneTable.gameFromJson(StandaloneTable.gameToJson(table.state.value.game))
        assertEquals(players, roundTrip.players)
        table.start()
        assertEquals(listOf("sigil:0:1", "sigil:1:1"), table.state.value.game.players.map { it.sigilSeat })
    }

    @Test fun `old device seats migrate without inventing a saved person`() {
        val old = StandaloneGame().addPlayer("Sigil 1", localId = "sigil:0:1")
        val json = StandaloneTable.gameToJson(old)
        val migrated = StandaloneTable.gameFromJson(json).players.single()
        assertNull(migrated.localId)
        assertTrue(migrated.guest)
        assertEquals("sigil:0:1", migrated.sigilSeat)
    }

    @Test fun `invalid and late join inputs cannot change a running roster`() {
        val table = StandaloneTable(MemoryStandaloneStore(StandaloneGame(), LocalLibrary()))
        assertFalse(table.applyHostedInput(0, action("sigil-join", id = 16)))
        assertFalse(table.applyHostedInput(0, action("sigil-join", slot = 0)))
        assertTrue(table.applyHostedInput(0, action("sigil-join")))
        assertTrue(table.applyHostedInput(0, action("sigil-join", id = 1)))
        table.start()
        assertFalse(table.applyHostedInput(0, action("sigil-join", id = 2)))
        assertEquals(2, table.state.value.game.players.size)
    }
}
