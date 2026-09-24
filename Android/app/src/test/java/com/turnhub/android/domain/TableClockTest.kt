package com.turnhub.android.domain

import com.turnhub.android.protocol.TableState
import com.turnhub.android.testing.Fixtures
import org.junit.Assert.assertEquals
import org.junit.Test

class TableClockTest {

    private fun summary(state: String = "RUNNING", edit: org.json.JSONObject.() -> Unit = {}) =
        TableSummaryMapper.map(
            Fixtures.info(),
            Fixtures.state("running.response.json") { put("state", state); edit() },
            receivedAtMs = 50_000,
        )

    @Test
    fun `running clocks advance by local time since the snapshot arrived`() {
        val running = summary { put("turnElapsedMs", 1_000).put("gameElapsedMs", 60_000) }

        assertEquals(1_000L, TableClock.turnElapsedMs(running, nowMs = 50_000))
        assertEquals(3_500L, TableClock.turnElapsedMs(running, nowMs = 52_500))
        assertEquals(62_500L, TableClock.gameElapsedMs(running, nowMs = 52_500))
    }

    @Test
    fun `paused and other states never advance`() {
        listOf("PAUSED", "LOBBY", "GAME_OVER", "STARTING").forEach { state ->
            val frozen = summary(state) { put("turnElapsedMs", 1_000) }
            assertEquals(state, TableState.valueOf(state), frozen.state)
            assertEquals(state, 1_000L, TableClock.turnElapsedMs(frozen, nowMs = 90_000))
        }
    }

    @Test
    fun `a clock that went backwards never subtracts`() {
        val running = summary { put("turnElapsedMs", 1_000) }
        assertEquals(1_000L, TableClock.turnElapsedMs(running, nowMs = 40_000))
    }

    @Test
    fun `pass grace counts down and stops at zero`() {
        val pending = summary {
            getJSONObject("pending").put("passPlayer", 1).put("passGraceRemainingMs", 3_000)
        }
        assertEquals(3_000L, TableClock.passGraceRemainingMs(pending, nowMs = 50_000))
        assertEquals(1_000L, TableClock.passGraceRemainingMs(pending, nowMs = 52_000))
        assertEquals(0L, TableClock.passGraceRemainingMs(pending, nowMs = 60_000))
        assertEquals(0L, TableClock.passGraceRemainingMs(summary(), nowMs = 50_000))
    }

    @Test
    fun `formats minutes and hours`() {
        assertEquals("0:00", TableClock.format(0))
        assertEquals("0:59", TableClock.format(59_999))
        assertEquals("12:05", TableClock.format(725_000))
        assertEquals("1:02:03", TableClock.format(3_723_000))
        assertEquals("0:00", TableClock.format(-5))
    }
}
