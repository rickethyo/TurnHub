package com.turnhub.android.ui.tablet

import com.turnhub.android.domain.TableSummaryMapper
import com.turnhub.android.protocol.GameProfile
import com.turnhub.android.protocol.PlayerCounters
import com.turnhub.android.protocol.TableState
import com.turnhub.android.testing.Fixtures
import org.junit.Assert.*
import org.junit.Test

class PublicPanelStatsTest {
    private fun summary() = com.turnhub.android.testing.TableFixtures.running(receivedAtMs = 1_000)

    @Test fun `only the active living panel identifies a turn and pause remains explicit`() {
        val base = summary()
        val player = base.players.first()
        val unit = TableUnit("one", listOf(player))
        val active = base.copy(activePlayerNumber = player.playerNumber)
        assertEquals("Active turn", publicTurnLabel(unit, active))
        assertEquals("Turn paused", publicTurnLabel(unit, active.copy(state = TableState.PAUSED)))
        assertNull(publicTurnLabel(unit, active.copy(state = TableState.GAME_OVER)))
        assertNull(publicTurnLabel(unit, active.copy(activePlayerNumber = null)))
        assertNull(publicTurnLabel(unit.copy(members = listOf(player.copy(eliminated = true))), active))
    }

    @Test fun `team turns count once and commander casts include both partners and teammates`() {
        val base = summary().let { it.copy(settings = it.settings.copy(profile = GameProfile.MTG_COMMANDER)) }
        val first = base.players.first().copy(turnsCompleted = 4, counters = PlayerCounters(commanderCasts = listOf(2, 1)))
        val second = base.players.last().copy(turnsCompleted = 4, counters = PlayerCounters(commanderCasts = listOf(1, 0)))
        assertEquals(listOf("Turns completed 4", "Commander casts 4"),
            publicPanelFacts(TableUnit("team", listOf(first, second)), base))
    }

    @Test fun `facts follow the format and do not fabricate missing prize or lore totals`() {
        val base = summary()
        fun facts(profile: GameProfile, life: Int?) = publicPanelFacts(
            TableUnit("one", listOf(base.players.first().copy(life = life, turnsCompleted = 2, stress = 3))),
            base.copy(settings = base.settings.copy(profile = profile, startingLife = 20)))
        assertEquals(listOf("Turns completed 2", "Prizes left 3"), facts(GameProfile.POKEMON, 3))
        assertEquals(listOf("Turns completed 2", "Lore 12 / 20"), facts(GameProfile.LORCANA, 12))
        assertEquals(listOf("Turns completed 2"), facts(GameProfile.POKEMON, null))
        assertEquals(listOf("Turns completed 2", "Stress 3"), facts(GameProfile.DEATH_MAY_DIE, 0))
        assertEquals(listOf("Turns completed 2"), facts(GameProfile.YUGIOH, 8000))
    }
}
