package com.turnhub.android.domain

import com.turnhub.android.protocol.TurnTimerPhase
import com.turnhub.android.testing.Fixtures
import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class TurnTimerStatusTest {

    /** The Atlas-generated fixture: 90-second timer, WARNING with 9 s left, 81 s elapsed. */
    private fun summary(edit: JSONObject.() -> Unit = {}) = TableSummaryMapper.map(
        Fixtures.info(),
        Fixtures.state("timer-warning.response.json", edit),
        receivedAtMs = 50_000,
    )

    @Test
    fun `countdown renders forward between polls and stops at zero`() {
        val warning = summary()

        assertEquals(9_000L, TableClock.turnRemainingMs(warning, nowMs = 50_000))
        assertEquals(6_500L, TableClock.turnRemainingMs(warning, nowMs = 52_500))
        assertEquals(0L, TableClock.turnRemainingMs(warning, nowMs = 70_000))

        val status = TurnTimerStatus.of(warning, nowMs = 52_500)!!
        assertEquals("Time left" to "0:06", status.label to status.value)
        assertEquals(TurnTimerPhase.WARNING, status.phase)
        assertTrue(status.notice!!.contains("Ten seconds"))
    }

    @Test
    fun `a paused countdown does not move`() {
        val paused = summary { put("state", "PAUSED") }
        assertEquals(9_000L, TableClock.turnRemainingMs(paused, nowMs = 90_000))
    }

    @Test
    fun `expiry shows overtime and says the turn continues`() {
        val expired = summary {
            put("turnElapsedMs", 95_000)
            getJSONObject("turnTimer").put("phase", "EXPIRED").put("remainingMs", 0)
        }

        val status = TurnTimerStatus.of(expired, nowMs = 51_000)!!
        assertEquals("Over time" to "+0:06", status.label to status.value)
        assertTrue(status.notice!!.contains("continues"))
    }

    @Test
    fun `timer off shows elapsed time and the long-turn cue`() {
        val untimed = summary {
            getJSONObject("settings").put("turnTimerMs", 0)
            getJSONObject("turnTimer").put("phase", "NORMAL").put("remainingMs", JSONObject.NULL)
        }
        assertEquals("Turn" to "1:21", TurnTimerStatus.of(untimed, 50_000)!!.let { it.label to it.value })
        assertNull(TurnTimerStatus.of(untimed, 50_000)!!.notice)

        val long = summary {
            put("turnElapsedMs", 300_000)
            getJSONObject("settings").put("turnTimerMs", 0)
            getJSONObject("turnTimer").put("phase", "LONG_TURN").put("remainingMs", JSONObject.NULL)
        }
        assertTrue(TurnTimerStatus.of(long, 50_000)!!.notice!!.contains("five minutes"))
    }

    @Test
    fun `older firmware without timer fields still shows elapsed time`() {
        val older = summary {
            remove("turnTimer")
            getJSONObject("settings").remove("turnTimerMs")
        }
        val status = TurnTimerStatus.of(older, 50_000)!!
        assertEquals("Turn", status.label)
        assertEquals(TurnTimerPhase.NORMAL, status.phase)
    }

    @Test
    fun `no turn clock outside a running or paused game`() {
        listOf("LOBBY", "STARTING", "GAME_OVER").forEach { state ->
            assertNull(state, TurnTimerStatus.of(summary { put("state", state) }, 50_000))
        }
    }

    @Test
    fun `setting labels`() {
        assertEquals("Off", TurnTimerStatus.settingLabel(0))
        assertEquals("1 minute", TurnTimerStatus.settingLabel(60_000))
        assertEquals("5 minutes", TurnTimerStatus.settingLabel(300_000))
        assertEquals("90 seconds", TurnTimerStatus.settingLabel(90_000))
    }
}
