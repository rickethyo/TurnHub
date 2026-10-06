package com.turnhub.android.data

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class ProfileStatisticsTest {

    // The shape handleProfileStats in Atlas/src/web_profile_api.cpp writes.
    private val body = """
        {"ok":true,"profileId":"P0001","name":"Ricky","winRate":"40.0%","detailed":true,"lastGameType":"commander",
         "lifetime":{"gamesPlayed":5,"gamesWon":2,"gamesStarted":1,"gamesEliminated":3,"turnsCompleted":40,
           "totalTurnMs":2400000,"averageTurnMs":60000,"fastestTurnMs":5000,"longestTurnMs":300000,
           "totalGameMs":18000000,"averageGameMs":3600000},
         "lastGame":{"result":"Won","durationMs":3723000,"turns":8,"turnMs":480000,"averageTurnMs":60000,
           "fastestTurnMs":10000,"longestTurnMs":120000},
         "moderation":{"visible":true,"connectionResets":1,"gameRemovals":0}}
    """.trimIndent()

    @Test
    fun parsesEveryField() {
        val stats = ProfileStatistics.parse(body)!!
        assertEquals("Ricky", stats.name)
        assertEquals("P0001", stats.profileId)
        assertEquals("40.0%", stats.winRate)
        assertTrue(stats.detailed)
        assertEquals(5L, stats.lifetime.gamesPlayed)
        assertEquals(2L, stats.lifetime.gamesWon)
        assertEquals(3600000L, stats.lifetime.averageGameMs)
        assertEquals("Won", stats.lastGame.result)
        assertEquals(8L, stats.lastGame.turns)
        assertTrue(stats.moderation.visible)
        assertEquals(1L, stats.moderation.connectionResets)
        assertNull(stats.moderation.reason)
        assertEquals(0.4f, stats.winFraction, 0.0001f)
    }

    @Test
    fun hiddenModerationKeepsTheReason() {
        val stats = ProfileStatistics.parse(
            """{"ok":true,"profileId":"P2","name":"","detailed":false,"lifetime":{},"lastGame":{},
               "moderation":{"visible":false,"reason":"Sign in with your PIN"}}""",
        )!!
        assertEquals("Unnamed profile", stats.name)
        assertFalse(stats.detailed)
        assertFalse(stats.moderation.visible)
        assertEquals("Sign in with your PIN", stats.moderation.reason)
        assertEquals("None", stats.lastGame.result)
        assertEquals(0f, stats.winFraction, 0f)
    }

    @Test
    fun notJsonIsNull() {
        assertNull(ProfileStatistics.parse("<html>"))
    }

    @Test
    fun durationMatchesThePortal() {
        assertEquals("1h 2m 3s", ProfileStatistics.duration(3_723_000))
        assertEquals("0m 5s", ProfileStatistics.duration(5_000))
        assertEquals("—", ProfileStatistics.duration(5_000, hasRecord = false))
    }
}
