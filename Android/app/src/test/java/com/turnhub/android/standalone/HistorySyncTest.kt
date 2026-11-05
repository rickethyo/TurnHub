package com.turnhub.android.standalone

import com.turnhub.android.data.TabletSeat
import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test

class HistorySyncTest {
    private var now = 1_700_000_000_000L
    private fun device() = StandaloneTable(MemoryStandaloneStore(StandaloneGame(), LocalLibrary()), wallClock = { now }, shortGameMs = 0)

    /** Plays the table's lobby to a finished game; seat 0 wins. */
    private fun play(table: StandaloneTable) {
        table.chooseStarter(0); table.start()
        now += 120_000
        assertTrue(kotlinx.coroutines.runBlocking { table.control(TabletSeat(StandaloneGame.seatHandle(0).id, 1), "win") })
    }

    /** What the guest runner does: pull the host's missing games, push its own. */
    private fun sync(guest: StandaloneTable, host: StandaloneTable, profileId: String): Pair<Int, Int> {
        fun step(name: String) = JSONObject().put("step", name)
        val there = host.roomHistory(profileId, step("index"))!!.getJSONArray("records").let { a -> (0 until a.length()).map { a.getString(it) } }.toSet()
        val here = guest.historyIds(profileId).toSet()
        var pulled = 0; var pushed = 0
        (there - here).chunked(HistorySync.PAGE).forEach { chunk ->
            val reply = host.roomHistory(profileId, step("pull").put("ids", JSONArray(chunk)))!!
            pulled += guest.mergeHistory(profileId, reply.getJSONArray("history"))!!.let { it.added + it.bound }
        }
        (here - there).chunked(HistorySync.PAGE).forEach { chunk ->
            pushed += host.roomHistory(profileId, step("push").put("history", guest.historyRecords(profileId, chunk)))!!.getInt("added")
        }
        return pulled to pushed
    }

    @Test fun `games played apart merge both ways once and stats follow`() {
        val phone = device()
        assertTrue(phone.createAccount("Alex"))
        val alex = phone.state.value.library.players.single()
        phone.selectPlayer(alex.localId); phone.addPlayer("Friend")
        play(phone) // away from the host

        val host = device()
        assertEquals(alex.profileId, host.attachRoomProfile(JSONObject().put("profile",
            JSONObject().put("profileId", alex.profileId).put("name", "Alex"))))
        host.addPlayer("Hosty")
        play(host) // at the host's table; the host's copy of Alex sits in seat 0

        assertEquals(1 to 1, sync(phone, host, alex.profileId))
        val hostAlex = host.state.value.library.players.first { it.profileId == alex.profileId }
        assertEquals(LocalTotals(2, 2, 0), phone.state.value.library.totals(alex.localId))
        assertEquals(LocalTotals(2, 2, 0), host.state.value.library.totals(hostAlex.localId))
        // Reconnecting again moves nothing and duplicates nothing.
        assertEquals(0 to 0, sync(phone, host, alex.profileId))
        assertEquals(2, phone.state.value.library.history.size)
        assertEquals(2, host.state.value.library.history.size)
        // The friend from the away game is not attached to any host account.
        val friend = phone.state.value.library.players.first { it.name == "Friend" }
        assertEquals(LocalTotals(0, 0, 0), host.state.value.library.players.firstOrNull { it.profileId == friend.profileId }
            ?.let { host.state.value.library.totals(it.localId) } ?: LocalTotals(0, 0, 0))
        // But the record names the friend by profile ID, so the friend's own sync can bind later.
        assertTrue(host.state.value.library.history.any { r -> r.players.any { it.profileId == friend.profileId } })
    }

    @Test fun `a differing copy of a known game is skipped, never replaces it`() {
        val phone = device()
        phone.createAccount("Alex")
        val alex = phone.state.value.library.players.single()
        phone.selectPlayer(alex.localId); phone.addPlayer("Friend")
        play(phone)
        val record = phone.historyRecords(alex.profileId, phone.historyIds(alex.profileId)).getJSONObject(0)
        val forged = JSONObject(record.toString()).put("winner", 1).put("winners", JSONArray().put(1))
        val merge = phone.mergeHistory(alex.profileId, JSONArray().put(forged))!!
        assertEquals(0, merge.added + merge.bound)
        assertEquals(1, merge.conflicts)
        assertEquals(LocalTotals(1, 1, 0), phone.state.value.library.totals(alex.localId))
    }

    @Test fun `a pushed game must name the syncing profile`() {
        val phone = device()
        phone.createAccount("Alex"); phone.createAccount("Sam")
        val (alex, sam) = phone.state.value.library.players
        phone.selectPlayer(alex.localId); phone.addPlayer("Friend")
        play(phone)
        val alexGame = phone.historyRecords(alex.profileId, phone.historyIds(alex.profileId))
        val host = device()
        host.attachRoomProfile(JSONObject().put("profile", JSONObject().put("profileId", sam.profileId).put("name", "Sam")))
        // Sam's member token cannot place Alex's game on Sam's account.
        val answer = host.roomHistory(sam.profileId, JSONObject().put("step", "push").put("history", alexGame))!!
        assertEquals(0, answer.getInt("added"))
        assertEquals(1, answer.getInt("conflicts"))
        assertTrue(host.state.value.library.history.isEmpty())
        // And an account the host doesn't hold has no history to exchange.
        assertNull(host.roomHistory(alex.profileId, JSONObject().put("step", "index")))
    }
}
