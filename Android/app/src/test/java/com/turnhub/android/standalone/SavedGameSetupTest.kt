package com.turnhub.android.standalone

import com.turnhub.android.protocol.GameProfile
import com.turnhub.android.protocol.TableState
import org.junit.Assert.*
import org.junit.Test

class SavedGameSetupTest {
    private class Store : StandaloneStore {
        var game: String? = null
        var library: String? = null
        var fail = false
        override fun loadGame() = game
        override fun loadLibrary() = library
        override fun loadRecords(): String? = null
        override fun saveSnapshot(game: String, library: String): Boolean {
            if (fail) return false
            this.game = game; this.library = library
            return true
        }
    }

    @Test fun `named setup survives reopening with players in seat order and rules`() {
        val store = Store()
        val configured = StandaloneGame(profile = GameProfile.MTG_COMMANDER, startingLife = 45,
            houseRules = true, turnTimerMs = 90_000, autoEliminateAtZeroLife = false,
            players = listOf(LocalPlayer("B", guest = true, commander = "Sol Ring"), LocalPlayer("A", guest = true)))
        store.game = StandaloneTable.gameToJson(configured).toString()
        val table = StandaloneTable(store)
        assertTrue(table.saveGameSetup("Friday"))
        table.clearPlayers()
        val reopened = StandaloneTable(store)
        assertTrue(reopened.loadGameSetup("Friday"))
        val game = reopened.state.value.game
        assertEquals(listOf("B", "A"), game.players.map { it.name })
        assertEquals("Sol Ring", game.players[0].commander)
        assertEquals(45, game.startingLife)
        assertTrue(game.houseRules)
        assertFalse(game.autoEliminateAtZeroLife)
        assertEquals(90_000L, game.turnTimerMs)
        assertEquals(TableState.LOBBY, game.state)
        assertTrue(reopened.state.value.library.history.isEmpty())
    }

    @Test fun `capture strips match state and scores`() {
        val match = StandaloneGame(state = TableState.GAME_OVER, gameId = "old-match", winner = 0,
            elapsedMs = 1234, startedAtMs = 99, monarch = 0,
            players = listOf(LocalPlayer("A", life = 3, eliminated = true, turnsCompleted = 10)))
        val setup = SavedGameSetup.fromJson(SavedGameSetup("Clean", match).toJson()).game
        assertEquals(TableState.LOBBY, setup.state)
        assertEquals("", setup.gameId)
        assertTrue(setup.winners.isEmpty())
        assertEquals(0L, setup.elapsedMs)
        assertNull(setup.monarch)
        assertFalse(setup.players.single().eliminated)
        assertEquals(0, setup.players.single().turnsCompleted)
    }

    @Test fun `same name requires explicit replacement and failed writes keep old setup`() {
        val store = Store()
        val table = StandaloneTable(store)
        table.addGuest("A")
        assertFalse(table.saveGameSetup(" "))
        assertTrue(table.saveGameSetup("Friday"))
        table.addGuest("B")
        assertFalse(table.saveGameSetup("friday"))
        store.fail = true
        assertFalse(table.saveGameSetup("Friday", replace = true))
        assertEquals(1, table.state.value.library.gameSetups.single().game.players.size)
        store.fail = false
        assertNotNull(table.state.value.storageProblem)
        val recovered = StandaloneTable(store)
        assertTrue(recovered.saveGameSetup("Friday", replace = true))
        assertEquals(2, recovered.state.value.library.gameSetups.single().game.players.size)
        assertTrue(recovered.deleteGameSetup("Friday"))
        assertTrue(StandaloneTable(store).state.value.library.gameSetups.isEmpty())
    }

    @Test fun `loading is refused during a game and when a saved identity was deleted`() {
        val store = Store()
        val table = StandaloneTable(store)
        table.addPlayer("A"); table.addGuest("B")
        val id = table.state.value.library.players.single().localId
        assertTrue(table.saveGameSetup("Friday"))
        assertTrue(table.deleteAccount(id))
        assertFalse(table.loadGameSetup("Friday"))
        store.game = StandaloneTable.gameToJson(StandaloneGame(state = TableState.RUNNING)).toString()
        val running = StandaloneTable(store)
        assertFalse(running.loadGameSetup("Friday"))
        assertFalse(running.saveGameSetup("Other"))
        assertEquals(TableState.RUNNING, running.state.value.game.state)
    }
}
