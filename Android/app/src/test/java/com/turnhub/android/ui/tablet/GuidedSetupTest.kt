package com.turnhub.android.ui.tablet

import com.turnhub.android.domain.InvestigatorCard
import com.turnhub.android.domain.InvestigatorCardSlot
import com.turnhub.android.protocol.GameProfile
import com.turnhub.android.standalone.StandaloneGame
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class GuidedSetupTest {
    @Test fun `the confirm screen lists each investigator's choices and what is still missing`() {
        val game = StandaloneGame().setFormat(GameProfile.DEATH_MAY_DIE).addPlayer("Ana").addPlayer("Ben").setUseRelics(true)
            .setInvestigator(0, "Rasputin")
            .setInvestigatorCard(0, InvestigatorCardSlot.SANITY_CONDITION, InvestigatorCard("Paranoia"))
        val summary = game.toSummary(0, 0)
        val (ana, ben) = summary.players.sortedBy { it.playerNumber }
        assertEquals(listOf("Rasputin", "Paranoia"), choicesFor(summary, ana))
        assertEquals(listOf("relic"), missingFor(summary, ana))
        assertEquals(listOf("investigator", "sanity condition", "relic"), missingFor(summary, ben))
        assertTrue(choicesFor(summary, ben).isEmpty())
    }

    @Test fun `other games need nothing before Start and show the tile card`() {
        val summary = StandaloneGame().addPlayer("Ana").addPlayer("Ben").toSummary(0, 0)
        assertTrue(summary.players.all { missingFor(summary, it).isEmpty() && choicesFor(summary, it).isEmpty() })
    }
}
