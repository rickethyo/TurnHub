package com.turnhub.android.standalone

import com.turnhub.android.data.TabletSeat
import com.turnhub.android.protocol.*
import kotlinx.coroutines.test.runTest
import org.junit.Assert.*
import org.junit.Test
import kotlin.random.Random

class DoomBagTest {
    private fun lobby(n: Int = 10) = (1..n).fold(StandaloneGame().setFormat(GameProfile.MTG_BAG_OF_DOOM)) { g, i -> g.addPlayer("Player $i") }.chooseStarter(2)
    private fun game(n: Int = 10) = lobby(n).start(1_000, "doom-match")
    private fun due(game: StandaloneGame = game()): StandaloneGame {
        var g = game
        repeat(g.players.count { !it.eliminated } * 4) { g = g.pass(g.active!!, 2_000L + it) }
        return g
    }
    private val first = object : Random() { override fun nextBits(bitCount: Int): Int = 0 }

    @Test fun `requires five players and keeps Commander rules without teams`() {
        assertFalse(lobby(4).canStart)
        assertEquals(TableState.LOBBY, lobby(4).start(1_000, "small").state)
        assertTrue(lobby(5).canStart)
        assertFalse(lobby().setTwoHeadedGiant(true).twoHeadedGiant)
        assertEquals(40, game().players[0].life)
        assertTrue(game().profile.commanderRules)
    }

    @Test fun `other life loss saves the turn owner and fourth full cycle pauses before next turn`() {
        val initial = game()
        assertEquals(2, initial.doomBag.boundary)
        assertTrue(initial.changeLife(2, -1, 1_100).doomBag.contains(3))
        val safe = initial.changeLife(3, -1, 1_100).changeLife(3, 1, 1_200)
        assertFalse(safe.doomBag.contains(3))
        val waiting = due(safe)
        assertEquals(TableState.PAUSED, waiting.state)
        assertTrue(waiting.doomBag.pending)
        assertEquals(2, waiting.doomBag.pendingTurn)
        assertEquals(2, waiting.active)
        assertSame(waiting, waiting.togglePause(9_000))
        assertSame(waiting, waiting.claimWin(0, 9_000))
        assertSame(waiting, waiting.drawDoom(99, 9_000, first))
        // Pending edits still belong to the turn that just ended.
        assertFalse(waiting.changeLife(3, -1, 9_000).doomBag.contains(2))
    }

    @Test fun `commander damage saves owner but its correction requires explicit bag correction`() {
        val hit = game().commanderDamage(3, 5, 1, 1, 1_100)
        assertFalse(hit.doomBag.contains(3))
        val undo = hit.commanderDamage(3, 5, 1, -1, 1_200)
        assertFalse(undo.doomBag.contains(3))
        assertTrue(undo.doomEligibility(3, true, undo.doomBag.cycle).doomBag.contains(3))
    }

    @Test fun `draw is saved with no kill credit resets immunity and rejects duplicate cycle`() {
        val waiting = due().doomEligibility(1, false, 1)
        val drawn = waiting.drawDoom(1, 9_000, first)
        assertEquals(2, drawn.doomBag.lastDraw)
        assertEquals(2L, drawn.doomBag.cycle)
        assertTrue(drawn.players[1].eliminated)
        assertNull(drawn.players[1].eliminatedBy)
        assertTrue(drawn.doomBag.contains(1))
        assertFalse(drawn.doomBag.contains(2))
        assertFalse(drawn.doomBag.pending)
        assertSame(drawn, drawn.drawDoom(1, 9_100, first))
        assertSame(drawn, drawn.doomCredit(2, 3, 2))
        assertEquals(drawn, StandaloneTable.gameFromJson(StandaloneTable.gameToJson(drawn)))
        val record = drawn.claimWin(0, 90_000).record()!!
        assertTrue(record.players[1].bagDrawn)
        assertEquals(0, record.eliminationsBy(0))
        assertEquals(record, GameRecord.fromJson(record.toJson()))
        assertNull(GameRecord.fromJson(record.toJson().apply {
            getJSONArray("players").getJSONObject(1).put("eliminatedBy", 0)
        }))
        assertFalse(drawn.claimWin(0, 90_000).rematch(91_000, "next").doomBag.pending)
    }

    @Test fun `empty bag check resets every survivor and next check is one full cycle later`() {
        var waiting = due()
        (1..10).forEach { waiting = waiting.doomEligibility(it, false, 1) }
        val checked = waiting.drawDoom(1, 9_000, first)
        assertEquals(0, checked.doomBag.lastDraw)
        assertEquals(1023, checked.doomBag.atRisk)
        var next = checked.togglePause(10_000)
        repeat(9) { next = next.pass(next.active!!, 11_000L + it) }
        assertFalse(next.doomBag.pending)
        next = next.pass(next.active!!, 12_000)
        assertTrue(next.doomBag.pending)
    }

    @Test fun `credited kills refill except killer move boundary and stop bag at two survivors`() {
        val initial = game(7)
        val killed = initial.eliminate(3, 2_000, eliminatedBy = 1)
        assertEquals(2L, killed.doomBag.cycle)
        assertEquals(0, killed.doomBag.warmup)
        assertEquals(1, killed.doomBag.boundary)
        assertFalse(killed.doomBag.contains(2))
        val natural = killed.changeLife(5, -40, 3_000)
        val credited = natural.resolveElimination(6, 2, false, 3_100)
        assertEquals(2, credited.players[5].eliminatedBy)
        assertEquals(2, credited.doomBag.boundary)
        assertSame(credited, credited.doomCredit(6, 2, 3))
        val four = credited.concede(6, 4_000)
        assertEquals(4, four.players.count { !it.eliminated })
        assertNotEquals(0, four.doomBag.atRisk)
        assertNotEquals(0, four.doomBag.boundary)
        val three = four.concede(4, 4_100)
        assertEquals(3, three.players.count { !it.eliminated })
        assertNotEquals(0, three.doomBag.boundary)
        val off = three.concede(2, 4_200)
        assertNull(off.winner)
        assertEquals(2, off.players.count { !it.eliminated })
        assertEquals(0, off.doomBag.atRisk)
        assertEquals(0, off.doomBag.boundary)
        assertEquals(off, StandaloneTable.gameFromJson(StandaloneTable.gameToJson(off)))
    }

    @Test fun `a game saved with the bag off at four survivors restarts it on load`() {
        val four = game(7).concede(3, 2_000).concede(5, 2_100).concede(6, 2_200)
        val old = four.copy(doomBag = four.doomBag.copy(atRisk = 0, boundary = 0, warmup = 0))
        val loaded = StandaloneTable.gameFromJson(StandaloneTable.gameToJson(old))
        assertEquals(DoomRules.bit(1) or DoomRules.bit(2) or DoomRules.bit(3) or DoomRules.bit(5), loaded.doomBag.atRisk)
        assertEquals(DoomRules.previous(loaded.active!! + 1, loaded.doomBag.atRisk, 7), loaded.doomBag.boundary)
        var g = loaded
        repeat(4) { g = g.pass(g.active!!, 3_000L + it) }
        assertTrue(g.doomBag.pending)
        assertEquals(TableState.PAUSED, g.state)
    }

    @Test fun `corrupt bag cannot silently recover or be imported into other formats`() {
        val valid = due()
        for (field in listOf("atRisk", "boundary", "pendingTurn", "cycle")) {
            val bad = StandaloneTable.gameToJson(valid).apply { getJSONObject("doomBag").put(field, -1) }
            assertTrue(runCatching { StandaloneTable.gameFromJson(bad) }.isFailure)
        }
        val badDrawn = StandaloneTable.gameToJson(valid).apply { getJSONObject("doomBag").put("drawn", 1) }
        assertTrue(runCatching { StandaloneTable.gameFromJson(badDrawn) }.isFailure)
        assertTrue(runCatching { StandaloneTable.gameFromJson(StandaloneTable.gameToJson(valid).apply { remove("doomBag") }) }.isFailure)
    }

    @Test fun `authority commits before reveal and rejects stale boot revision and duplicate requests`() = runTest {
        val waiting = due()
        class Store : StandaloneStore {
            var saved = StandaloneTable.gameToJson(waiting).toString()
            var fail = false
            var committed = 0
            override fun loadGame() = saved
            override fun loadRecords(): String? = null
            override fun loadLibrary(): String? = null
            override fun saveSnapshot(game: String, library: String): Boolean { saved = game; return true }
            override fun commitSnapshot(game: String, library: String): Boolean {
                committed++
                if (fail) return false
                saved = game; return true
            }
        }
        val store = Store()
        val table = StandaloneTable(store, wallClock = { 9_000 })
        val seat = TabletSeat(StandaloneGame.seatHandle(0).id, 1)
        val token = GameActionToken(waiting.gameId, waiting.revision, 1)
        assertFalse(table.doom(seat, DoomAction.DRAW, token.copy(bootId = "old")))
        assertFalse(table.doom(seat, DoomAction.DRAW, token.copy(revision = 0)))
        assertTrue(table.doom(seat, DoomAction.DRAW, token))
        assertEquals(1, store.committed)
        assertEquals(9, table.state.value.game.players.count { !it.eliminated })
        assertFalse(table.doom(seat, DoomAction.DRAW, token))
        assertEquals(table.state.value.game, StandaloneTable.gameFromJson(org.json.JSONObject(store.saved)))
        val failedStore = Store().apply { fail = true }
        val failed = StandaloneTable(failedStore, wallClock = { 9_000 })
        assertFalse(failed.doom(seat, DoomAction.DRAW, token))
        assertEquals(waiting, failed.state.value.game)
        assertNotNull(failed.state.value.storageProblem)
    }
}
