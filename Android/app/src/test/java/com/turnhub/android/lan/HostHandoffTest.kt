package com.turnhub.android.lan

import com.turnhub.android.standalone.*
import com.turnhub.android.protocol.TableState
import com.turnhub.android.data.TabletSeat
import kotlinx.coroutines.runBlocking
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test

class HostHandoffTest {
    private class FaultStore : StandaloneStore {
        val saved = MemoryStandaloneStore(StandaloneGame(), LocalLibrary())
        var writable = true
        override fun loadGame() = saved.loadGame()
        override fun loadLibrary() = saved.loadLibrary()
        override fun loadRecords(): String? = null
        override fun saveSnapshot(game: String, library: String) = writable && saved.saveSnapshot(game, library)
    }
    private class Fixture {
        val store = MemoryStandaloneStore(StandaloneGame(), LocalLibrary())
        var table = StandaloneTable(store).apply { addGuest("One"); addGuest("Two"); start() }
        var record = JSONObject()
        var saved = "{}"
        var writable = true
        fun link() = HostHandoff(record, { if (writable) { saved = record.toString(); true } else false }, table)
        fun restart(): HostHandoff {
            record = JSONObject(saved); table = StandaloneTable(store)
            return link()
        }
    }
    @Test fun `only the chosen member can collect and old authority is fenced durably`() = runBlocking {
        val f = Fixture(); val original = f.table.state.value.game
        var link = f.link()
        assertTrue(link.offer("chosen"))
        val id = requireNotNull(link.offerFor("chosen"))
        assertNull(link.offerFor("other"))
        assertNull(link.accept("other", id))
        assertFalse(f.table.life(TabletSeat(StandaloneGame.seatHandle(0).id, 1), 1))
        assertEquals(original.players, f.table.state.value.game.players)
        link = f.restart()
        assertEquals(id, f.table.state.value.game.hostTransfer)
        val snapshot = requireNotNull(link.accept("chosen", id))
        assertEquals(TableState.LOBBY, f.table.state.value.game.state)
        assertTrue(f.table.state.value.game.players.isEmpty())
        assertEquals(original.gameId, StandaloneTable.gameFromJson(snapshot).gameId)
        assertFalse(link.cancel())
        assertFalse(link.collectedBy("other", id))
        assertTrue(link.collectedBy("chosen", id))
    }
    @Test fun `lost handoff reply and both process restarts never rewind the receiver`() = runBlocking {
        val f = Fixture(); var link = f.link()
        assertTrue(link.offer("chosen")); val id = requireNotNull(link.offerFor("chosen"))
        val original = requireNotNull(link.accept("chosen", id)) // reply is lost
        link = f.restart()
        val retry = requireNotNull(link.accept("chosen", id))
        assertEquals(original.toString(), retry.toString())
        val store = MemoryStandaloneStore(StandaloneGame(), LocalLibrary())
        var receiver = StandaloneTable(store)
        assertTrue(receiver.receiveTransferredGame(id, retry))
        assertTrue(receiver.life(TabletSeat(StandaloneGame.seatHandle(0).id, 1), 3))
        receiver = StandaloneTable(store)
        assertTrue(receiver.receiveTransferredGame(id, retry))
        assertEquals(43, receiver.state.value.game.players.first().life)
    }
    @Test fun `failed room persistence cannot grant ownership or erase the recoverable game`() {
        val f = Fixture(); val link = f.link()
        assertTrue(link.offer("chosen")); val id = requireNotNull(link.offerFor("chosen"))
        f.writable = false
        assertNull(link.accept("chosen", id))
        assertTrue(link.released)
        assertFalse(link.cancel())
        f.writable = true
        assertNotNull(f.restart().accept("chosen", id))
    }
    @Test fun `cancel before acceptance restores editing and leaves every player intact`() = runBlocking {
        val f = Fixture(); val link = f.link()
        val before = f.table.state.value.game
        assertTrue(link.offer("chosen")); assertTrue(link.cancel())
        assertEquals(before, f.table.state.value.game)
        assertTrue(f.table.life(TabletSeat(StandaloneGame.seatHandle(0).id, 1), 1))
        assertNull(link.accept("chosen", "old-offer"))
    }
    @Test fun `receiving never replaces another running game`() {
        val source = Fixture(); val link = source.link()
        assertTrue(link.offer("chosen")); val id = requireNotNull(link.offerFor("chosen"))
        val game = requireNotNull(link.accept("chosen", id))
        val receiver = Fixture().table; val before = receiver.state.value.game
        assertFalse(receiver.receiveTransferredGame(id, game))
        assertEquals(before, receiver.state.value.game)
    }
    @Test fun `failed game commits retain collection data across storage recovery`() {
        val store = FaultStore()
        var source = StandaloneTable(store).apply { addGuest("One"); addGuest("Two"); start() }
        var record = JSONObject(); var saved = "{}"
        fun link() = HostHandoff(record, { saved = record.toString(); true }, source)
        var handoff = link()
        assertTrue(handoff.offer("chosen")); val id = requireNotNull(handoff.offerFor("chosen"))
        store.writable = false
        assertNull(handoff.accept("chosen", id))
        assertEquals(TableState.RUNNING, source.state.value.game.state)
        store.writable = true
        source = StandaloneTable(store); record = JSONObject(saved); handoff = link()
        val game = requireNotNull(handoff.accept("chosen", id))
        val destination = FaultStore().apply { writable = false }
        assertFalse(StandaloneTable(destination).receiveTransferredGame(id, game))
        assertFalse(handoff.collected)
        destination.writable = true
        assertTrue(StandaloneTable(destination).receiveTransferredGame(id, requireNotNull(handoff.accept("chosen", id))))
        assertTrue(handoff.collectedBy("chosen", id))
    }
    @Test fun `profile identities remap by public ID without copying source accounts or matching names`() {
        val alice = SavedLocalPlayer("source-a", "Sam", "ABCDEF01", credential = LocalCredential.create("1234"))
        val bob = SavedLocalPlayer("source-b", "Sam", "ABCDEF02")
        val game = StandaloneGame(players = listOf(LocalPlayer("Sam", localId = alice.localId), LocalPlayer("Sam", localId = bob.localId)))
        val source = StandaloneTable(MemoryStandaloneStore(game, LocalLibrary(players = listOf(alice, bob))))
        val record = JSONObject(); val handoff = HostHandoff(record, { true }, source)
        assertTrue(handoff.offer("chosen")); val id = requireNotNull(handoff.offerFor("chosen"))
        val json = requireNotNull(handoff.accept("chosen", id))
        assertFalse(json.toString().contains("credential"))
        assertFalse(json.toString().contains("source-a"))
        val unrelated = SavedLocalPlayer("receiver-a", "Sam", "ABCDEF03")
        val matching = SavedLocalPlayer("receiver-b", "Renamed Bob", bob.profileId)
        val library = LocalLibrary(players = listOf(unrelated, matching))
        val target = StandaloneTable(MemoryStandaloneStore(StandaloneGame(), library))
        assertTrue(target.receiveTransferredGame(id, json))
        assertNull(target.state.value.game.players[0].localId)
        assertEquals("receiver-b", target.state.value.game.players[1].localId)
        assertEquals(library, target.state.value.library)
    }
}
