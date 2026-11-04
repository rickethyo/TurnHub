package com.turnhub.android.ui.tablet

import com.turnhub.android.domain.TableSummaryMapper
import com.turnhub.android.lan.LanUiState
import com.turnhub.android.protocol.GameProfile
import com.turnhub.android.testing.Fixtures
import org.junit.Assert.*
import org.junit.Test

class SharedPlanarControlsTest {
    @Test fun `roll button follows active turn and team even for a seated master`() {
        val base = com.turnhub.android.testing.TableFixtures.running(receivedAtMs = 1000)
        val summary = base.copy(activePlayerNumber = base.players.first().playerNumber,
            settings = base.settings.copy(profile = GameProfile.MTG_COMMANDER, planechase = true))
        val state = LanUiState(role = "player", player = summary.players.first().playerNumber, online = true)
        assertTrue(sharedPlanarRollEnabled(state, summary))
        val other = state.copy(player = summary.players[1].playerNumber)
        assertFalse(sharedPlanarRollEnabled(other, summary))
        assertFalse(sharedPlanarRollEnabled(other.copy(master = true), summary))
        assertTrue(sharedPlanarEnabled(other, summary))
        assertFalse(sharedPlanarRollEnabled(state, summary.copy(state = com.turnhub.android.protocol.TableState.PAUSED)))
        assertTrue(sharedPlanarRollEnabled(other, summary.copy(players = summary.players.map { it.copy(team = 1) })))
        assertTrue(sharedPlanarRollEnabled(state.copy(role = "table", player = 0), summary))
    }
    @Test fun `phone hosted snapshot carries its actual Planechase setting`() {
        val game = com.turnhub.android.standalone.StandaloneGame().setFormat(GameProfile.MTG_COMMANDER)
            .setPlanechase(true).addPlayer("A").addPlayer("B")
        val summary = game.toSummary(1000, 1000)
        assertTrue(summary.settings.planechase)
        assertTrue(sharedPlanarEnabled(LanUiState(role = "player", player = 1, online = true), summary))
    }
    @Test fun `living player controls planes without master but offline eliminated and public viewers cannot`() {
        val base = com.turnhub.android.testing.TableFixtures.running(receivedAtMs = 1000)
        val summary = base.copy(settings = base.settings.copy(profile = GameProfile.MTG_COMMANDER, planechase = true))
        val state = LanUiState(role = "player", player = summary.players.first().playerNumber, online = true)
        assertTrue(sharedPlanarEnabled(state, summary))
        assertFalse(sharedPlanarEnabled(state.copy(online = false), summary))
        assertFalse(sharedPlanarEnabled(state.copy(busy = true), summary))
        assertFalse(sharedPlanarEnabled(state.copy(uncertain = true), summary))
        assertFalse(sharedPlanarEnabled(state, summary.copy(players = summary.players.map { it.copy(eliminated = true) })))
        assertFalse(sharedPlanarEnabled(state, summary.copy(settings = summary.settings.copy(planechase = false))))
        for (role in listOf("display", "planes")) assertFalse(sharedPlanarEnabled(state.copy(role = role), summary))
        assertTrue(sharedPlanarEnabled(state.copy(role = "table", player = 0), summary))
        assertTrue(sharedPlanarEnabled(state.copy(role = "planes", master = true), summary))
    }
}
