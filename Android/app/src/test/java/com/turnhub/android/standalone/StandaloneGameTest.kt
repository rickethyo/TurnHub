package com.turnhub.android.standalone

import com.turnhub.android.data.RawResponse
import com.turnhub.android.data.TabletSeat
import com.turnhub.android.protocol.GameProfile
import com.turnhub.android.protocol.TableState
import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertSame
import org.junit.Assert.assertTrue
import org.junit.Test
import kotlin.random.Random

class StandaloneGameTest {

    /** Always starts with player 1 (index 0). */
    private val firstSeat = object : Random() {
        override fun nextBits(bitCount: Int): Int = 0
    }

    private fun threePlayers(): StandaloneGame = StandaloneGame()
        .setFormat(GameProfile.MTG_COMMANDER)
        .addPlayer("Ana", "p-ana")
        .addPlayer("Ben")
        .addPlayer("Cy")
        .start(nowMs = 1_000, gameId = "g1", random = firstSeat)

    @Test
    fun `a game starts everyone on the format's life with a first player`() {
        val game = threePlayers()
        assertEquals(TableState.RUNNING, game.state)
        assertEquals(listOf(40, 40, 40), game.players.map { it.life })
        assertEquals(0, game.active)
        assertEquals(0, game.starter)
    }

    @Test
    fun `the lobby needs two players and refuses duplicates of one profile`() {
        val one = StandaloneGame().addPlayer("Ana", "p-ana")
        assertSame(one, one.start(0, "g", firstSeat))
        assertSame(one, one.addPlayer("Ana again", "p-ana"))
        assertSame(one, one.addPlayer("   "))
    }

    @Test
    fun `turn order can be changed in the lobby`() {
        val game = StandaloneGame().addPlayer("Ana").addPlayer("Ben").movePlayer(1, -1)
        assertEquals(listOf("Ben", "Ana"), game.players.map { it.name })
    }

    @Test
    fun `passing moves the turn clockwise and counts the finished turn`() {
        val game = threePlayers().pass(0, 61_000)
        assertEquals(1, game.active)
        assertEquals(1, game.players[0].turnsCompleted)
        assertEquals(60_000, game.turnStartedAtElapsedMs)
        assertEquals(60_000, game.players[0].turnMs)
        assertEquals(60_000, game.players[0].fastestTurnMs)
        // Only the active player passes.
        assertSame(game, game.pass(0, 62_000))
    }

    @Test
    fun `commander damage costs life and never goes below zero`() {
        val game = threePlayers().commanderDamage(index = 0, source = 1, commander = 1, delta = 7)
        assertEquals(33, game.players[0].life)
        assertEquals(listOf(7, 0), game.players[0].commanderDamage[1])
        val healed = game.commanderDamage(0, 1, 1, -10)
        assertEquals(listOf(0, 0), healed.players[0].commanderDamage[1])
        assertEquals(40, healed.players[0].life)
        assertSame(game, game.commanderDamage(0, 0, 1, 1))
    }

    @Test
    fun `conceding skips the player and the last one standing wins`() {
        val game = threePlayers().concede(0, 2_000)
        assertEquals(1, game.active)
        assertTrue(game.players[0].eliminated)
        assertEquals(1, game.players[0].outOrder)
        val over = game.concede(2, 3_000)
        assertEquals(TableState.GAME_OVER, over.state)
        assertEquals(1, over.winner)
        assertEquals(2_000, over.elapsedMs)
    }

    @Test
    fun `pausing stops the game clock`() {
        val paused = threePlayers().togglePause(11_000)
        assertEquals(10_000, paused.gameElapsedMs(50_000))
        val resumed = paused.togglePause(50_000)
        assertEquals(15_000, resumed.gameElapsedMs(55_000))
    }

    @Test
    fun `a finished game becomes a record for Atlas`() {
        val record = threePlayers().changeLife(1, -5).pass(0, 5_000).claimWin(1, 9_000).record()!!
        assertEquals("g1", record.recordId)
        assertEquals(1, record.winner)
        assertEquals(8_000, record.durationMs)
        assertEquals("p-ana", record.players[0].profileId)
        assertNull(record.players[1].profileId)
        assertEquals(35, record.players[1].finalLife)
        assertEquals(record, GameRecord.fromJson(record.toJson()))
    }

    @Test
    fun `the table keeps the game and each finished game once`() = runTest {
        val store = MemoryStore()
        var clock = 1_000L
        var ids = 0
        val table = StandaloneTable(store, { clock }, { "g${++ids}" })
        table.addPlayer("Ana")
        table.addPlayer("Ben")
        table.start()
        val first = table.state.value.game.active!!
        clock = 4_000
        table.life(TabletSeat(StandaloneGame.seatHandle(first).id, 1), -3)
        table.control(TabletSeat(StandaloneGame.seatHandle(first).id, 1), "win")
        assertEquals(1, table.state.value.records.size)
        table.control(TabletSeat(StandaloneGame.seatHandle(first).id, 1), "win")
        assertEquals(1, table.state.value.records.size)

        // A new table on the same store picks up where this one stopped.
        val again = StandaloneTable(store, { clock }, { "g${++ids}" })
        assertEquals(table.state.value.game, again.state.value.game)
        assertEquals(table.state.value.records, again.state.value.records)

        again.control(TabletSeat(StandaloneGame.seatHandle(0).id, 1), "rematch")
        assertEquals(TableState.RUNNING, again.state.value.game.state)
        again.control(TabletSeat(StandaloneGame.seatHandle(0).id, 1), "draw")
        assertEquals(2, again.state.value.records.size)
        assertNull(again.state.value.records[1].winner)
        again.forgetRecords(setOf("g1"))
        assertEquals(listOf("g2"), again.state.value.records.map { it.recordId })
    }

    @Test
    fun `finished games go to Atlas once and wait when it can't take them`() = runTest {
        var ids = 0
        val table = StandaloneTable(MemoryStore(), { 1_000L }, { "game-${++ids}" })
        table.addPlayer("Ana", "p-ana")
        table.addPlayer("Ben")
        repeat(3) {
            table.start()
            table.control(TabletSeat(StandaloneGame.seatHandle(0).id, 1), "draw")
            table.control(TabletSeat(StandaloneGame.seatHandle(0).id, 1), "reset")
        }
        assertEquals(3, table.state.value.records.size)
        val fields = table.state.value.records[0].importFields().toMap()
        assertEquals("game-1", fields["recordId"])
        assertEquals("2", fields["players"])
        assertEquals("", fields["winner"])
        assertEquals("p-ana", fields["profile0"])
        assertEquals("", fields["profile1"])

        // Taken, refused as invalid, then Atlas can't save: the last one waits.
        val answers = ArrayDeque(listOf(RawResponse(200, "{}"), RawResponse(400, "{}"), RawResponse(503, "{}")))
        assertEquals(1, table.sendRecords { answers.removeFirst() })
        assertEquals(listOf("game-3"), table.state.value.records.map { it.recordId })
        assertEquals(0, table.sendRecords { null })
        assertEquals(1, table.state.value.records.size)
    }

    private class MemoryStore : StandaloneStore {
        var game: String? = null
        var records: String? = null
        var profiles: String? = null
        override fun loadGame() = game
        override fun saveGame(json: String) { game = json }
        override fun loadRecords() = records
        override fun saveRecords(json: String) { records = json }
        override fun loadProfiles() = profiles
        override fun saveProfiles(json: String) { profiles = json }
    }
}
