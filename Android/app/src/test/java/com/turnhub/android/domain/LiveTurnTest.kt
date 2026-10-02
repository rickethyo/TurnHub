package com.turnhub.android.domain

import com.turnhub.android.testing.Fixtures
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class LiveTurnTest {

    private fun summary(state: String = "RUNNING", edit: org.json.JSONObject.() -> Unit = {}) =
        TableSummaryMapper.map(
            Fixtures.info(),
            Fixtures.state("running.response.json") { put("state", state); edit() },
            receivedAtMs = 50_000,
        )

    @Test
    fun `the turn clock is pinned to wall time`() {
        val running = summary { put("turnElapsedMs", 4_000) }

        val mine = LiveTurn.from(running, myNumber = 1, nowMs = 51_000, wallNowMs = 1_000_000)!!
        assertTrue(mine.mine)
        assertEquals(1L, mine.turnNumber)
        assertEquals(1_000_000L - 5_000, mine.turnStartedAtMs)
        assertNull(mine.timerEndsAtMs)

        val theirs = LiveTurn.from(running, myNumber = 2, nowMs = 51_000, wallNowMs = 1_000_000)!!
        assertFalse(theirs.mine)
        assertEquals(mine.activeName, theirs.activeName)
    }

    @Test
    fun `nothing to show unless this phone's player sits in a live game`() {
        assertNull(LiveTurn.from(summary(), myNumber = null, nowMs = 50_000, wallNowMs = 0))
        assertNull(LiveTurn.from(summary(), myNumber = 7, nowMs = 50_000, wallNowMs = 0))
        assertNull(LiveTurn.from(summary("LOBBY"), myNumber = 1, nowMs = 50_000, wallNowMs = 0))
        assertNull(LiveTurn.from(summary("GAME_OVER"), myNumber = 1, nowMs = 50_000, wallNowMs = 0))

        val paused = LiveTurn.from(summary("PAUSED"), myNumber = 1, nowMs = 50_000, wallNowMs = 0)!!
        assertNull(paused.turnStartedAtMs)
    }

    @Test
    fun `poll-to-poll drift is the same moment, a new turn is not`() {
        val a = LiveTurn.from(summary { put("turnElapsedMs", 4_000) }, 1, 50_000, 1_000_000)!!
        val drift = LiveTurn.from(summary { put("turnElapsedMs", 5_000) }, 1, 50_000, 1_001_300)!!
        assertTrue(drift.sameMoment(a))

        val next = LiveTurn.from(summary { put("activePlayer", 2) }, 1, 50_000, 1_000_000)!!
        assertFalse(next.sameMoment(a))
    }
}
