package com.turnhub.android.ui.tablet

import com.turnhub.android.standalone.StandaloneGame
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class GuidedSetupTest {
    @Test fun `the confirm screen shows each player's tile card`() {
        val summary = StandaloneGame().addPlayer("Ana").addPlayer("Ben").setCommander(0, "Atraxa").toSummary(0, 0)
        val (ana, ben) = summary.players.sortedBy { it.playerNumber }
        assertEquals(listOf("Atraxa"), choicesFor(summary, ana))
        assertTrue(choicesFor(summary, ben).isEmpty())
    }
}
