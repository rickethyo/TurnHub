package com.turnhub.android.domain

import com.turnhub.android.protocol.TableState
import com.turnhub.android.testing.TableFixtures
import org.junit.Assert.assertEquals
import org.junit.Test

class TableClockTest {

    private fun summary(state: String = "RUNNING", edit: TableSummary.() -> TableSummary = { this }) =
        TableFixtures.running(state = TableState.valueOf(state)).edit()

    @Test
    fun `running clocks advance by local time since the snapshot arrived`() {
        val running = summary { copy(turnElapsedMs = 1_000, gameElapsedMs = 60_000) }

        assertEquals(1_000L, TableClock.turnElapsedMs(running, nowMs = 50_000))
        assertEquals(3_500L, TableClock.turnElapsedMs(running, nowMs = 52_500))
        assertEquals(62_500L, TableClock.gameElapsedMs(running, nowMs = 52_500))
    }

    @Test
    fun `paused and other states never advance`() {
        listOf("PAUSED", "LOBBY", "GAME_OVER", "STARTING").forEach { state ->
            val frozen = summary(state) { copy(turnElapsedMs = 1_000) }
            assertEquals(state, TableState.valueOf(state), frozen.state)
            assertEquals(state, 1_000L, TableClock.turnElapsedMs(frozen, nowMs = 90_000))
        }
    }

    @Test
    fun `a clock that went backwards never subtracts`() {
        val running = summary { copy(turnElapsedMs = 1_000) }
        assertEquals(1_000L, TableClock.turnElapsedMs(running, nowMs = 40_000))
    }

    @Test
    fun `pass grace counts down and stops at zero`() {
        val pending = summary { copy(pending = pending.copy(passPlayer = 1, passGraceRemainingMs = 3_000)) }
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
