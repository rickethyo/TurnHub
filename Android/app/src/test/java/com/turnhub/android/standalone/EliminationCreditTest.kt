package com.turnhub.android.standalone

import com.turnhub.android.protocol.GameProfile
import com.turnhub.android.protocol.TableState
import org.junit.Assert.*
import org.junit.Test

class EliminationCreditTest {
    private fun table(count: Int = 3): StandaloneGame = (0 until count).fold(StandaloneGame()) { g, i ->
        val added = g.addPlayer("Player $i")
        added.copy(players = added.players.mapIndexed { n, p -> p.copy(localId = "local-$n") })
    }.chooseStarter(0).start(1_000, "credit-match")

    @Test fun `ten players start and the eleventh is refused and winner nine survives export`() {
        val running = table(10)
        assertEquals(10, running.players.size)
        assertEquals(TableState.RUNNING, running.state)
        val lobby = running.reset()
        assertSame(lobby, lobby.addPlayer("Eleventh"))
        var game = running
        repeat(10) { expected ->
            assertEquals(expected, game.active)
            game = game.pass(expected, 2_000L + expected * 1_000)
        }
        assertEquals(0, game.active)
        val record = game.claimWin(9, 90_000).record()!!
        assertEquals(9, GameRecord.fromJson(record.toJson())!!.winner)
        assertEquals("9", record.importFields("atlas", List(10) { "id$it" }).toMap()["winners"])
        assertEquals(10, StandaloneTable.gameFromJson(StandaloneTable.gameToJson(game)).players.size)
    }

    @Test fun `credit survives saves and final removal and is counted by player identity`() {
        val running = table()
        assertSame(running, running.eliminate(1, 2_000, eliminatedBy = 1))
        assertSame(running, running.eliminate(1, 2_000, eliminatedBy = 3))
        assertSame(running, running.eliminate(1, 2_000, conceded = true, eliminatedBy = 0))
        val first = running.eliminate(1, 2_000, eliminatedBy = 0)
        assertSame(first, first.eliminate(1, 3_000, eliminatedBy = 0))
        val saved = StandaloneTable.gameFromJson(StandaloneTable.gameToJson(first))
        assertEquals(0, saved.players[1].eliminatedBy)
        assertEquals(1, saved.toSummary(3_000, 0).players[1].eliminatedBy)
        val ended = saved.eliminate(2, 90_000, eliminatedBy = 0)
        val record = GameRecord.fromJson(ended.record()!!.toJson())!!
        assertEquals(2, record.eliminationsBy(0))
        val library = LocalLibrary(history = listOf(record))
        assertEquals(2, library.stats("local-0").eliminationsMade)
        assertEquals(0, library.stats("local-1").eliminationsMade)
        assertEquals("0", record.importFields("atlas", List(3) { "id$it" }).toMap()["eliminatedBy2"])
        assertTrue(record.shareSummary("Commander").contains("eliminated by Player 0"))
        assertNull(GameRecord.fromJson(record.toJson().apply {
            getJSONArray("players").getJSONObject(1).put("eliminatedBy", 1)
        }))
        assertEquals(listOf(null, null, null), ended.rematch(100_000, "next").players.map { it.eliminatedBy })
    }

    @Test fun `automatic removal and concession award no speculative credit and teams reject teammate credit`() {
        val running = table()
        assertNull(running.changeLife(1, -40, 3_000).players[1].eliminatedBy)
        assertNull(running.concede(1, 3_000).players[1].eliminatedBy)
        val teams = table(4).reset().setTwoHeadedGiant(true).start(1_000, "teams")
        assertSame(teams, teams.eliminate(2, 3_000, eliminatedBy = 3))
        val record = teams.eliminate(2, 90_000, eliminatedBy = 0).record()!!
        assertEquals(2, record.eliminationsBy(0))
        assertEquals(listOf(0, 0), record.players.drop(2).map { it.eliminatedBy })
    }
}
