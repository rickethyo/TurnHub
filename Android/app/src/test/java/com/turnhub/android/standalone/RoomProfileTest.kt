package com.turnhub.android.standalone

import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test

class RoomProfileTest {
    @Test fun `first link verifies PIN and cannot overwrite an existing same ID credential`() {
        val table = StandaloneTable(MemoryStandaloneStore(StandaloneGame(), LocalLibrary()))
        assertTrue(table.createAccount("Alex", LocalCredential.create("1234")))
        val player = table.state.value.library.players.single()
        val request = JSONObject().put("profileId", player.profileId).put("pin", "wrong")
        assertNull(table.attachRoomProfile(request))
        request.put("pin", "1234")
        assertEquals(player.profileId, table.attachRoomProfile(request))
        assertEquals(player.localId, table.state.value.game.players.single().localId)
        assertEquals(1, table.state.value.library.players.size)
    }
    @Test fun `injected profiles preserve identity and same names do not merge`() {
        val table = StandaloneTable(MemoryStandaloneStore(StandaloneGame(), LocalLibrary()))
        fun offered(id: String) = JSONObject().put("profile", JSONObject().put("profileId", id).put("name", "Sam"))
        assertEquals("AABBCCDD", table.attachRoomProfile(offered("AABBCCDD")))
        assertEquals("11223344", table.attachRoomProfile(offered("11223344")))
        assertEquals(2, table.state.value.library.players.size)
        assertEquals(2, table.state.value.game.players.size)
    }
    @Test fun `profile sync carries the newer look without moving identity or secret`() {
        val table = StandaloneTable(MemoryStandaloneStore(StandaloneGame(), LocalLibrary()))
        val request = JSONObject().put("profile", JSONObject().put("profileId", "AABBCCDD").put("name", "Before"))
        assertEquals("AABBCCDD", table.attachRoomProfile(request))
        val localId = table.state.value.library.players.single().localId
        request.getJSONObject("profile").put("name", "After").put("activity", Long.MAX_VALUE / 2)
            .put("look", LocalPersonalization(color = "#123456").toJson())
        assertEquals("AABBCCDD", table.attachRoomProfile(request))
        assertEquals(localId, table.state.value.library.players.single().localId)
        assertEquals("#123456", table.state.value.library.players.single().personalization.color)
    }
}
