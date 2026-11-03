package com.turnhub.android.lan

import com.turnhub.android.data.TabletSeat
import com.turnhub.android.domain.SpecialCondition
import com.turnhub.android.protocol.GameProfile
import com.turnhub.android.standalone.*
import kotlinx.coroutines.runBlocking
import org.junit.Assert.*
import org.junit.Test

class LanTableControlsTest {
    private class Fixture(profile: GameProfile) {
        val game = StandaloneGame().setFormat(profile).addPlayer("A").addPlayer("B")
            .chooseStarter(0).start(1_000, "shared")
        val table = StandaloneTable(MemoryStandaloneStore(game, LocalLibrary()), wallClock = { 2_000 })
        val summary = game.toSummary(1_000, 1_000)
        val controls = LanTableControls(summary, act = { kind, value, slot, source, target ->
            val current = table.state.value.game
            assertTrue("$kind for $target", table.applyLanAction(LanAction(current.gameId, current.revision, target, kind, value, slot, source)))
        }, refused = { fail("Ordinary shared-panel actions must not be host-only") })
        fun seat(player: Int) = summary.players.first { it.playerNumber == player }.let { TabletSeat(it.controller.id, it.slot) }
    }
    @Test fun `shared Pokemon panels apply a condition to the selected player through real validators`() = runBlocking {
        val f = Fixture(GameProfile.POKEMON)
        f.controls.counter(f.seat(2), StandaloneGame.CONDITION_KIND + SpecialCondition.POISONED.name, 1)
        assertTrue(f.table.state.value.game.players[0].turn.conditions.isEmpty())
        assertEquals(setOf(SpecialCondition.POISONED), f.table.state.value.game.players[1].turn.conditions)
    }
    @Test fun `shared Commander panels route life counters and received damage to the chosen seat`() = runBlocking {
        val f = Fixture(GameProfile.MTG_COMMANDER)
        f.controls.life(f.seat(2), -2)
        f.controls.counter(f.seat(2), "energy", 1)
        f.controls.commander(f.seat(2), 1, 1, 1)
        val players = f.table.state.value.game.players
        assertEquals(40, players[0].life)
        assertEquals(37, players[1].life)
        assertEquals(0, players[0].counters.energy)
        assertEquals(1, players[1].counters.energy)
        assertEquals(listOf(1, 0), players[1].commanderDamage[0])
    }
}
