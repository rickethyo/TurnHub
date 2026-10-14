package com.turnhub.android.domain

import com.turnhub.android.protocol.TableState
import com.turnhub.android.standalone.StandaloneGame
import com.turnhub.android.testing.Fixtures
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test
import kotlin.random.Random

class GameSoundEventsTest {
    private val firstSeat = object : Random() { override fun nextBits(bitCount: Int) = 0 }
    private fun lobby() = StandaloneGame().addPlayer("Ana").addPlayer("Ben")
    private fun connected() = TableSummaryMapper.map(Fixtures.info(), Fixtures.state("running.response.json"), receivedAtMs = 50_000)
    private fun GameSoundEvents.game(game: StandaloneGame, enabled: Boolean = true) =
        observe(GameSoundSnapshot.from(game.toSummary(1_000, 0)), enabled)
    private fun GameSoundEvents.table(table: TableSummary, enabled: Boolean = true) =
        observe(GameSoundSnapshot.from(table), enabled)

    @Test fun `local game actions sound only after the engine confirms the transition`() {
        val events = GameSoundEvents()
        var game = lobby()
        assertNull(events.game(game))
        game = game.start(1_000, "match", firstSeat)
        assertEquals(GameSoundCue.START, events.game(game)) // Local match ID changes at start.
        assertNull(events.game(game.pass(1, 2_000))) // Wrong player cannot pass.
        game = game.pass(0, 3_000)
        assertEquals(GameSoundCue.PASS, events.game(game))
        assertNull(events.game(game))
        game = game.togglePause(4_000)
        assertEquals(GameSoundCue.PAUSE, events.game(game))
        game = game.togglePause(5_000)
        assertEquals(GameSoundCue.RESUME, events.game(game))
        game = game.claimWin(1, 6_000)
        assertEquals(GameSoundCue.WIN, events.game(game))
        assertNull(events.game(game))
        game = game.rematch(7_000, "rematch")
        assertEquals(GameSoundCue.START, events.game(game))
        game = game.endWithoutWinner(8_000)
        assertEquals(GameSoundCue.DRAW, events.game(game))
    }

    @Test fun `clock polls names artwork settings and life edits stay quiet`() {
        val events = GameSoundEvents()
        val game = lobby().start(1_000, "match", firstSeat)
        assertNull(events.game(game))
        assertNull(events.game(game.changeLife(0, -1)))
        val summary = game.toSummary(10_000, 5_000)
        assertNull(events.table(summary))
        assertNull(events.table(summary.copy(receivedAtMs = 90_000, turnElapsedMs = 80_000,
            players = summary.players.map { it.copy(label = "New name", color = "#123ABC") })))
    }

    @Test fun `Atlas pending pass and repeated snapshots are quiet then turn change sounds once`() {
        val events = GameSoundEvents()
        val table = connected()
        assertNull(events.table(table))
        val pending = table.copy(pending = table.pending.copy(passPlayer = table.activePlayerNumber))
        assertNull(events.table(pending))
        val next = table.copy(revision = table.revision + 1, activePlayerNumber = 2,
            players = table.players.map { if (it.playerNumber == 1) it.copy(turnsCompleted = it.turnsCompleted + 1) else it })
        assertEquals(GameSoundCue.PASS, events.table(next))
        assertNull(events.table(next.copy(receivedAtMs = next.receivedAtMs + 1_000)))
    }

    @Test fun `a team pass and several missed turns each emit one cue`() {
        val events = GameSoundEvents()
        val game = lobby().addPlayer("Cy").addPlayer("Dee").setTwoHeadedGiant(true).start(1_000, "teams", firstSeat)
        assertNull(events.game(game))
        val passed = game.pass(1, 2_000)
        assertEquals(GameSoundCue.PASS, events.game(passed))
        val round = passed.pass(2, 3_000).pass(0, 4_000)
        // Same team as the preceding observation, but completed-turn counters advanced.
        assertEquals(GameSoundCue.PASS, events.game(round))
        assertNull(events.game(round))
    }

    @Test fun `initial load unmute foreground resume and reconnect never replay events`() {
        val events = GameSoundEvents()
        val game = lobby().start(1_000, "match", firstSeat)
        assertNull(events.game(game))
        val first = game.pass(0, 2_000)
        assertNull(events.game(first, enabled = false))
        val second = first.pass(1, 3_000)
        assertNull(events.game(second, enabled = true))
        assertEquals(GameSoundCue.PASS, events.game(second.pass(0, 4_000)))
        events.reset()
        assertNull(events.game(second.pass(0, 4_000)))
        assertNull(events.observe(null, true))
        assertNull(events.game(second.pass(0, 4_000)))
    }

    @Test fun `switching Atlas game or boot and revision rollback take a fresh baseline`() {
        val events = GameSoundEvents()
        val table = connected()
        assertNull(events.table(table))
        val other = table.copy(game = table.game + 1, activePlayerNumber = 2)
        assertNull(events.table(other))
        val reboot = other.copy(bootId = "reboot", state = TableState.PAUSED)
        assertNull(events.table(reboot))
        val resumed = reboot.copy(state = TableState.RUNNING)
        assertEquals(GameSoundCue.RESUME, events.table(resumed))
        val rollback = resumed.copy(revision = resumed.revision - 1, activePlayerNumber = 1)
        assertNull(events.table(rollback))
        assertNull(events.table(rollback.copy(atlasId = "another-atlas", activePlayerNumber = 2)))
    }

    @Test fun `countdown cancellation is quiet and game over wins over a simultaneous turn change`() {
        val events = GameSoundEvents()
        val running = connected()
        assertNull(events.table(running.copy(state = TableState.LOBBY)))
        assertNull(events.table(running.copy(state = TableState.STARTING)))
        assertNull(events.table(running.copy(state = TableState.LOBBY)))
        assertEquals(GameSoundCue.START, events.table(running))
        assertEquals(GameSoundCue.WIN, events.table(running.copy(state = TableState.GAME_OVER,
            activePlayerNumber = null, winnerPlayerNumber = null, winnerPlayerNumbers = listOf(1, 2))))
        assertNull(events.table(running.copy(state = TableState.GAME_OVER,
            activePlayerNumber = null, winnerPlayerNumber = null, winnerPlayerNumbers = listOf(1, 2))))
    }
}
