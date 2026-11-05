package com.turnhub.android.standalone

import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test

class HostedSigilJoinTest {
    private fun action(kind: String, id: Int = 0, slot: Int = 1) = JSONObject()
        .put("kind", kind).put("sigil", id).put("slot", slot)

    @Test fun `device seats join persist publish and leave through real shared rules`() {
        val store = MemoryStandaloneStore(StandaloneGame(), LocalLibrary())
        val table = StandaloneTable(store, wallClock = { 1000 })
        assertTrue(table.applyHostedInput(0, action("sigil-join")))
        assertTrue(table.applyHostedInput(0, action("sigil-join", slot = 2)))
        assertEquals(listOf("sigil:0:1", "sigil:0:2"), table.state.value.game.players.map { it.localId })
        assertFalse(table.applyHostedInput(0, action("sigil-join")))
        val reloaded = StandaloneTable(store)
        assertEquals(2, reloaded.state.value.game.players.size)
        assertTrue(reloaded.state.value.library.players.isEmpty()) // No fake saved person/account.
        val published = StandaloneTable.gameFromJson(StandaloneTable.gameToJson(reloaded.state.value.game))
        assertEquals("sigil:0:1", published.players[0].localId)
        assertTrue(reloaded.applyHostedInput(0, action("sigil-leave", slot = 0)))
        assertTrue(reloaded.state.value.game.players.isEmpty())
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
