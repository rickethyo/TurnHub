package com.turnhub.android.ui.tablet

import com.turnhub.android.domain.TableSummaryMapper
import com.turnhub.android.lan.LanUiState
import com.turnhub.android.protocol.GameProfile
import com.turnhub.android.testing.Fixtures
import org.junit.Assert.*
import org.junit.Test

class SharedPlanarControlsTest {
    @Test fun `phone hosted snapshot carries its actual Planechase setting`() {
        val game = com.turnhub.android.standalone.StandaloneGame().setFormat(GameProfile.MTG_COMMANDER)
            .setPlanechase(true).addPlayer("A").addPlayer("B")
        val summary = game.toSummary(1000, 1000)
        assertTrue(summary.settings.planechase)
        assertTrue(sharedPlanarEnabled(LanUiState(role = "player", player = 1, online = true), summary))
    }
    @Test fun `living player controls planes without master but offline eliminated and public viewers cannot`() {
        val base = TableSummaryMapper.map(Fixtures.info(), Fixtures.state("running.response.json"), receivedAtMs = 1000)
        val summary = base.copy(settings = base.settings.copy(profile = GameProfile.MTG_COMMANDER, planechase = true))
        val state = LanUiState(role = "player", player = summary.players.first().playerNumber, online = true)
        assertTrue(sharedPlanarEnabled(state, summary))
        assertFalse(sharedPlanarEnabled(state.copy(online = false), summary))
        assertFalse(sharedPlanarEnabled(state.copy(busy = true), summary))
        assertFalse(sharedPlanarEnabled(state.copy(uncertain = true), summary))
        assertFalse(sharedPlanarEnabled(state, summary.copy(players = summary.players.map { it.copy(eliminated = true) })))
        assertFalse(sharedPlanarEnabled(state, summary.copy(settings = summary.settings.copy(planechase = false))))
        for (role in listOf("display", "planes", "elder")) assertFalse(sharedPlanarEnabled(state.copy(role = role), summary))
        assertTrue(sharedPlanarEnabled(state.copy(role = "table", player = 0), summary))
        assertTrue(sharedPlanarEnabled(state.copy(role = "planes", master = true), summary))
    }
}
