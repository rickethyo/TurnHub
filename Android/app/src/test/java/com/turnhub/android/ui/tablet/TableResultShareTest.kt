package com.turnhub.android.ui.tablet

import com.turnhub.android.protocol.TableState
import com.turnhub.android.standalone.StandaloneGame
import org.junit.Assert.*
import org.junit.Test

class TableResultShareTest {
    private fun finished() = StandaloneGame().addPlayer("A").addPlayer("B").addPlayer("C")
        .toSummary(0, 0).copy(state = TableState.GAME_OVER, gameElapsedMs = 90_000,
            winnerPlayerNumber = 2, winnerPlayerNumbers = listOf(2))

    @Test fun `a connected snapshot shares its host-reported winners and life without a local record`() {
        val summary = finished().copy(atlasId = "THA-real-table", players = finished().players.map {
            it.copy(life = 17 + it.playerNumber, turnsCompleted = 5)
        })
        val text = summary.resultShareSummary()
        assertTrue(text.startsWith("🏆 B won!"))
        assertTrue(text.contains("B: 19 life · 5 turns · winner"))
        assertFalse(text.contains(summary.atlasId))
    }

    @Test fun `shared victories include every winner and ignore player list order`() {
        val base = finished()
        val text = base.copy(winnerPlayerNumbers = listOf(1, 3), winnerPlayerNumber = 1,
            players = base.players.reversed()).resultShareSummary()
        assertTrue(text.contains("shared the win!"))
        assertTrue(text.contains("A"))
        assertTrue(text.contains("C"))
        val lines = text.lines()
        assertTrue(lines.indexOfFirst { it.startsWith("A") } < lines.indexOfFirst { it.startsWith("B") })
    }

    @Test fun `no winner is shared as a draw`() {
        assertTrue(finished().copy(winnerPlayerNumber = null, winnerPlayerNumbers = emptyList())
            .resultShareSummary().startsWith("Game over: a draw!"))
    }

    @Test(expected = IllegalArgumentException::class)
    fun `an unfinished game cannot be presented as a finished result`() {
        finished().copy(state = TableState.RUNNING).resultShareSummary()
    }
}
