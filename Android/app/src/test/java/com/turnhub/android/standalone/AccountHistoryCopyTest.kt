package com.turnhub.android.standalone

import com.turnhub.android.data.TabletSeat
import kotlinx.coroutines.test.runTest
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test

class AccountHistoryCopyTest {
    private class Store : StandaloneStore {
        var game: String? = null
        var library: String? = null
        override fun loadGame() = game
        override fun loadRecords(): String? = null
        override fun loadLibrary() = library
        override fun saveSnapshot(game: String, library: String): Boolean {
            this.game = game; this.library = library; return true
        }
    }

    @Test fun `a visiting profile keeps creation time statistics and match IDs across copies`() = runTest {
        var now = 1_700_000_000_000L
        val phone = StandaloneTable(Store(), wallClock = { now }, shortGameMs = 0)
        phone.createAccount("Visitor")
        val original = phone.state.value.library.players.single()
        phone.selectPlayer(original.localId)
        phone.addPlayer("Host")
        phone.chooseStarter(0); phone.start()
        now += 120_000
        assertTrue(phone.control(TabletSeat(StandaloneGame.seatHandle(0).id, 1), "win"))
        val copy = phone.accountCopy(original.localId)
        assertFalse(copy.toString().contains(original.localId))

        val destinationStore = Store()
        val destination = StandaloneTable(destinationStore, wallClock = { now + 10_000_000 }, shortGameMs = 0)
        assertEquals(StandaloneTable.CopyResult.ADDED, destination.addCopiedAccount(copy, false))
        val visiting = destination.state.value.library.players.single()
        assertNotEquals(original.localId, visiting.localId)
        assertEquals(original.profileId, visiting.profileId)
        assertEquals(original.createdAtMs, visiting.createdAtMs)
        assertEquals(LocalTotals(1, 1, 0), destination.state.value.library.totals(visiting.localId))
        assertEquals(phone.state.value.library.history.single().recordId, destination.state.value.library.history.single().recordId)
        assertEquals(StandaloneTable.CopyResult.ALREADY_HERE, destination.addCopiedAccount(copy, false))
        assertEquals(1, destination.state.value.library.history.size)
        assertEquals(original.createdAtMs, StandaloneTable(destinationStore).state.value.library.players.single().createdAtMs)
    }

    @Test fun `later imports never make a known profile younger and legacy unknown dates stay unknown`() {
        val old = SavedLocalPlayer("old-device", "A", profileId = "1234ABCD", createdAtMs = 1_700_000_000_000L)
        val table = StandaloneTable(Store())
        table.addCopiedAccount(AccountCopy.toJson(old), false)
        table.addCopiedAccount(AccountCopy.toJson(old.copy(createdAtMs = old.createdAtMs + 10_000)), false)
        assertEquals(old.createdAtMs, table.state.value.library.players.single().createdAtMs)
        val legacy = AccountCopy.toJson(old).also { it.remove("createdAtMs") }
        assertEquals(0L, AccountCopy.fromJson(legacy, "elsewhere").createdAtMs)
    }

    @Test fun `malformed copied history cannot partially create a profile`() {
        val table = StandaloneTable(Store())
        val copy = AccountCopy.toJson(SavedLocalPlayer("source", "A", profileId = "1234ABCD"))
            .put("history", org.json.JSONArray().put(JSONObject().put("recordId", "broken")))
        assertEquals(StandaloneTable.CopyResult.NOT_SAVED, table.addCopiedAccount(copy, false))
        assertTrue(table.state.value.library.players.isEmpty())
    }
}
