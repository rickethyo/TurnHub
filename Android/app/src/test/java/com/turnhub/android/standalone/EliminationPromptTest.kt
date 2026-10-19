package com.turnhub.android.standalone

import com.turnhub.android.data.TabletSeat
import com.turnhub.android.protocol.GameActionToken
import com.turnhub.android.protocol.GameProfile
import com.turnhub.android.protocol.TableState
import kotlinx.coroutines.test.runTest
import org.junit.Assert.*
import org.junit.Test
import org.json.JSONObject

class EliminationPromptTest {
    private fun game(n: Int = 3, bag: Boolean = false): StandaloneGame = (1..n).fold(
        StandaloneGame(askEliminationSource = true).setFormat(if (bag) GameProfile.MTG_BAG_OF_DOOM else GameProfile.MTG_COMMANDER)) { g, i ->
        g.addPlayer("Player $i", localId = "player-$i")
    }.chooseStarter(0).start(1_000, "explicit-source")

    @Test fun `off turn source is explicit and final elimination waits before history and statistics`() {
        val waiting = game().changeLife(1, -40, 2_000)
        assertEquals(TableState.PAUSED, waiting.state)
        assertTrue(waiting.players[1].eliminationPending)
        assertNull(waiting.players[1].eliminatedBy)
        assertSame(waiting, waiting.togglePause(2_100))
        assertSame(waiting, waiting.claimWin(0, 2_100))
        val credited = waiting.resolveElimination(2, 2, false, 2_200)
        assertEquals(2, credited.players[1].eliminatedBy)
        assertEquals(0, credited.active)
        val last = credited.togglePause(2_300).changeLife(0, -40, 90_000)
        assertEquals(TableState.PAUSED, last.state)
        assertNull(last.record())
        val restored = StandaloneTable.gameFromJson(StandaloneTable.gameToJson(last))
        assertEquals(last, restored)
        val finished = restored.resolveElimination(1, null, true, 91_000)
        assertEquals(TableState.GAME_OVER, finished.state)
        assertTrue(finished.players[0].selfEliminated)
        val record = finished.record()!!
        assertEquals(1, record.eliminationsBy(2))
        assertEquals(1, LocalLibrary(history = listOf(record)).stats("player-3").eliminationsMade)
        assertTrue(record.players[0].selfEliminated)
        assertTrue(record.shareSummary("Commander").contains("eliminated by self"))
        assertEquals(record, GameRecord.fromJson(record.toJson()))
        assertEquals("1", record.importFields("atlas", listOf("a", "b", "c")).toMap()["selfEliminated0"])
        assertTrue(finished.rematch(92_000, "next").players.none { it.selfEliminated || it.eliminationPending })
    }

    @Test fun `self and unknown give no bag reset while an off turn opponent moves the boundary`() {
        val waiting = game(7, true).changeLife(1, -40, 2_000)
        val self = waiting.resolveElimination(2, null, true, 2_100)
        assertEquals(1L, self.doomBag.cycle)
        assertSame(self, self.doomCredit(2, 3, 1))
        val next = self.togglePause(2_200).changeLife(3, -40, 3_000).resolveElimination(4, 2, false, 3_100)
        assertEquals(2L, next.doomBag.cycle)
        assertEquals(1, next.doomBag.boundary) // Player 2 is out: living predecessor of Player 3 is Player 1.
        assertFalse(next.doomBag.contains(3))
        val unknown = next.changeLife(5, -40, 4_000).resolveElimination(6, null, false, 4_100)
        assertFalse(unknown.players[5].eliminationPending)
        assertFalse(unknown.players[5].selfEliminated)
        assertEquals(2L, unknown.doomBag.cycle)
        assertEquals(4, unknown.players.count { !it.eliminated })
    }

    @Test fun `all out waits for all answers and a team shares its source without teammate credit`() {
        val all = game(2).changeLife(0, -40, 2_000).changeLife(1, -40, 2_000)
        assertEquals(TableState.PAUSED, all.state)
        val one = all.resolveElimination(1, null, true, 2_100)
        assertNull(one.record())
        assertEquals(TableState.GAME_OVER, one.resolveElimination(2, null, false, 2_200).state)
        val team = game(4).reset().setTwoHeadedGiant(true).start(1_000, "team-source").changeLife(2, -60, 2_000)
        assertSame(team, team.resolveElimination(3, 3, false, 2_100))
        val finished = team.resolveElimination(3, 0, false, 90_000)
        assertEquals(TableState.GAME_OVER, finished.state)
        assertEquals(listOf(0, 0), finished.players.drop(2).map { it.eliminatedBy })
        assertEquals(2, finished.record()!!.eliminationsBy(0))
    }

    @Test fun `manual self is distinct from unknown and incompatible metadata is refused`() {
        val self = game().eliminate(1, 2_000, self = true)
        assertTrue(self.players[1].selfEliminated)
        assertFalse(self.players[1].eliminationPending)
        assertEquals(self, StandaloneTable.gameFromJson(StandaloneTable.gameToJson(self)))
        assertTrue(runCatching { StandaloneTable.gameFromJson(StandaloneTable.gameToJson(self).apply {
            getJSONArray("players").getJSONObject(1).put("eliminatedBy", 0)
        }) }.isFailure)
        val running = game()
        assertSame(running, running.eliminate(1, 2_000, eliminatedBy = 0, self = true))
    }

    @Test fun `production starts enable prompts and stale or duplicate answers cannot change credit`() = runTest {
        val store = MemoryStandaloneStore(game().reset(), LocalLibrary())
        val table = StandaloneTable(store, wallClock = { 90_000 })
        table.start()
        assertTrue(table.state.value.game.askEliminationSource)
        val target = TabletSeat(StandaloneGame.seatHandle(1).id, 1)
        assertTrue(table.life(target, -40))
        val waiting = table.state.value.game
        val token = GameActionToken(waiting.gameId, waiting.revision, 0)
        assertFalse(table.resolveElimination(target, 2, 3, token.copy(bootId = "old")))
        assertTrue(table.resolveElimination(target, 2, 3, token))
        assertFalse(table.resolveElimination(target, 2, 1, token))
        assertEquals(2, table.state.value.game.players[1].eliminatedBy)
        assertEquals(table.state.value.game, StandaloneTable.gameFromJson(JSONObject(store.loadGame())))
    }
}
