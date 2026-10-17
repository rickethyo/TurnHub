package com.turnhub.android.standalone

import com.turnhub.android.domain.DeathMayDie
import com.turnhub.android.domain.cooperativeTurn
import com.turnhub.android.protocol.GameProfile
import com.turnhub.android.protocol.TableState
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertSame
import org.junit.Assert.assertTrue
import org.junit.Test
import kotlin.random.Random

class DeathMayDieTest {
    /** Player 1 always starts. */
    private object First : Random() {
        override fun nextBits(bitCount: Int): Int = 0
    }

    private fun lobby(players: Int = 3) =
        (1..players).fold(StandaloneGame().setFormat(GameProfile.DEATH_MAY_DIE)) { g, i -> g.addPlayer("P$i") }

    private fun game(players: Int = 3) = lobby(players).start(0, "d", First)

    @Test fun `death may die is a phone-hosted cooperative game counting wounds from zero`() {
        assertFalse(GameProfile.DEATH_MAY_DIE.atlasSupported)
        assertTrue(GameProfile.DEATH_MAY_DIE.cooperative)
        assertFalse(GameProfile.DEATH_MAY_DIE.countsLife)
        val g = game()
        assertEquals(listOf(0, 0, 0), g.players.map { it.life })
        assertSame(g, g.changeLife(0, -1))
        val hurt = g.changeLife(0, 3)
        assertEquals(3, hurt.players[0].life)
        // Wounds never knock anyone out: the dashboard decides.
        assertEquals(TableState.RUNNING, hurt.changeLife(0, 40).state)
    }

    @Test fun `each round tracks who has taken their turn and starts over once everyone has`() {
        val g = game()
        assertEquals(1, g.round)
        assertTrue(g.acted.isEmpty())
        val one = g.pass(0, 1_000)
        assertEquals(setOf(0), one.acted)
        val two = one.pass(1, 2_000)
        assertEquals(setOf(0, 1), two.acted)
        val summary = two.toSummary(2_000, 0)
        assertEquals(listOf(true, true, false), summary.players.map { it.actedThisRound })
        assertEquals(1, summary.round)
        val next = two.pass(2, 3_000)
        assertEquals(2, next.round)
        assertTrue(next.acted.isEmpty())
        assertEquals(0, next.active)
    }

    @Test fun `any player still to go can take the next turn`() {
        val g = game(4).pass(0, 1_000)
        assertEquals(1, g.active)
        // Player 3 goes instead; player 2 still hasn't gone.
        val jumped = g.changeCounter(2, StandaloneGame.TAKE_TURN_KIND, 1, nowMs = 1_500)
        assertEquals(2, jumped.active)
        // Someone who has gone, or already holds the turn, can't take it.
        assertSame(jumped, jumped.takeTurn(0, 1_600))
        assertSame(jumped, jumped.takeTurn(2, 1_600))
        // After player 3, the turn goes to the next player still to go: player 4, then player 2.
        val three = jumped.pass(2, 2_000)
        assertEquals(3, three.active)
        val four = three.pass(3, 3_000)
        assertEquals(1, four.active)
        val round = four.pass(1, 4_000)
        assertEquals(2, round.round)
        assertTrue(round.acted.isEmpty())
    }

    @Test fun `a fallen investigator no longer holds up the round`() {
        val two = game().pass(0, 1_000).pass(1, 2_000)
        // The last player still to go dies on their turn: the round is over.
        val fallen = two.eliminate(2, 2_500)
        assertEquals(TableState.RUNNING, fallen.state)
        assertEquals(2, fallen.round)
        assertEquals(0, fallen.active)
    }

    @Test fun `passing to a chosen investigator completes exactly one turn and accounts for its time`() {
        val g = game(4)
        assertEquals(listOf(2, 3, 4), g.cooperativePassTargets())
        assertEquals(g.cooperativePassTargets(), g.toSummary(0, 0).passPlayerNumbers)
        assertEquals(g.cooperativeTurn(), g.toSummary(0, 0).cooperativeTurn())
        val passed = g.passTo(g.cooperativeTurn()!!, 4, 1_500)
        assertEquals(3, passed.active)
        assertEquals(setOf(0), passed.acted)
        assertEquals(1, passed.round)
        assertEquals(listOf(1, 0, 0, 0), passed.players.map { it.turnsCompleted })
        assertEquals(1_500L, passed.players[0].turnMs)
        assertEquals(1_500L, passed.players[0].fastestTurnMs)
        assertEquals(1_500L, passed.turnStartedAtElapsedMs)
        assertEquals(0L, passed.toSummary(1_500, 0).turnElapsedMs)
        assertEquals(listOf(2, 3), passed.cooperativePassTargets())
    }

    @Test fun `invalid recipients cannot finish a turn or move its clock`() {
        val g = game().passTo(game().cooperativeTurn()!!, 3, 1_000)
        // P1 has gone; P3 currently holds the turn; only P2 is eligible.
        assertEquals(listOf(2), g.cooperativePassTargets())
        listOf(0, 1, 3, 99).forEach { target ->
            assertSame(g, g.passTo(g.cooperativeTurn()!!, target, 2_000))
        }
    }

    @Test fun `the last turn offers every survivor and the chosen player starts the new round`() {
        val g = game().pass(0, 1_000).pass(1, 2_000)
        assertEquals(listOf(1, 2, 3), g.cooperativePassTargets())
        val next = g.passTo(g.cooperativeTurn()!!, 3, 3_000)
        assertEquals(2, next.round)
        assertTrue(next.acted.isEmpty())
        assertEquals(2, next.active)
        assertEquals(listOf(1, 1, 1), next.players.map { it.turnsCompleted })
        assertEquals(listOf(1, 2), next.cooperativePassTargets())
    }

    @Test fun `fallen investigators are never offered even across the round boundary`() {
        val g = game(4).eliminate(1, 500).pass(0, 1_000)
        assertEquals(listOf(4), g.cooperativePassTargets())
        assertSame(g, g.passTo(g.cooperativeTurn()!!, 2, 1_500))
        val last = g.passTo(g.cooperativeTurn()!!, 4, 2_000)
        assertEquals(listOf(1, 3, 4), last.cooperativePassTargets())
        assertEquals(2, last.passTo(last.cooperativeTurn()!!, 1, 3_000).round)
    }

    @Test fun `a stale pass chooser cannot act on a different game holder round or completed turn`() {
        val g = game()
        val turn = g.cooperativeTurn()!!
        listOf(turn.copy(gameId = "other"), turn.copy(playerNumber = 2), turn.copy(round = 2),
            turn.copy(turnsCompleted = 1)).forEach { stale -> assertSame(g, g.passTo(stale, 2, 1_000)) }
        val jumped = g.takeTurn(1, 500)
        assertSame(jumped, jumped.passTo(turn, 3, 1_000))
        val passed = g.passTo(turn, 2, 1_000)
        assertSame(passed, passed.passTo(turn, 3, 2_000))
        // A wound correction doesn't finish or invalidate the current turn.
        val corrected = g.changeLife(0, 1)
        assertEquals(1, corrected.passTo(turn, 2, 1_000).active)
    }

    @Test fun `pass selection respects pause game over and profile guards`() {
        val g = game()
        val turn = g.cooperativeTurn()!!
        listOf(g.togglePause(500), g.claimWin(0, 500), lobby(),
            StandaloneGame().addPlayer("A").addPlayer("B").start(0, "d", First)).forEach { stopped ->
            assertNull(stopped.cooperativeTurn())
            assertTrue(stopped.cooperativePassTargets().isEmpty())
            assertSame(stopped, stopped.passTo(turn, 2, 1_000))
        }
    }

    @Test fun `an automatic timeout while choosing still completes only one turn`() {
        val g = game().copy(turnTimerMs = 1_000,
            timerRules = com.turnhub.android.domain.LocalTimerRules(timeout = com.turnhub.android.domain.TimeoutAction.PASS))
        val settled = g.passTo(g.cooperativeTurn()!!, 3, 2_000)
        // The already-due configured auto-pass wins; the old chooser doesn't pass its new turn.
        assertEquals(1, settled.active)
        assertEquals(listOf(1, 0, 0), settled.players.map { it.turnsCompleted })
        assertEquals(setOf(0), settled.acted)
    }

    @Test fun `one survivor can choose themself and pass-to results survive recovery`() {
        val g = game(2).eliminate(1, 500)
        assertEquals(listOf(1), g.cooperativePassTargets())
        val next = g.passTo(g.cooperativeTurn()!!, 1, 1_000)
        assertEquals(0, next.active)
        assertEquals(2, next.round)
        assertEquals(1, next.players[0].turnsCompleted)
        val restored = StandaloneTable.gameFromJson(StandaloneTable.gameToJson(next))
        assertEquals(next.cooperativeTurn(), restored.cooperativeTurn())
        assertEquals(listOf(1), restored.cooperativePassTargets())
        assertEquals(3, restored.passTo(restored.cooperativeTurn()!!, 1, 2_000).round)
    }

    @Test fun `the investigators win and lose together`() {
        val won = game().eliminate(1, 1_000).claimWin(0, 2_000)
        assertEquals(TableState.GAME_OVER, won.state)
        assertEquals(listOf(0, 1, 2), won.winners)
        // One investigator left standing doesn't win alone.
        val last = game().eliminate(0, 1_000).eliminate(1, 2_000)
        assertEquals(TableState.RUNNING, last.state)
        val lost = last.eliminate(2, 3_000)
        assertEquals(TableState.GAME_OVER, lost.state)
        assertTrue(lost.winners.isEmpty())
        assertTrue(game().endWithoutWinner(1_000).winners.isEmpty())
    }

    @Test fun `each investigator is played by one player, and survives a rematch and saving`() {
        val picked = lobby().setInvestigator(0, "Rasputin").setInvestigator(1, " Sister Beth ")
        assertEquals(listOf("Rasputin", "Sister Beth", null), picked.players.map { it.investigator })
        assertSame(picked, picked.setInvestigator(2, "rasputin"))
        assertNull(picked.setInvestigator(0, " ").players[0].investigator)
        val running = picked.start(0, "d", First).pass(0, 1_000)
        assertEquals("Rasputin", running.toSummary(1_000, 0).players[0].investigator)
        val restored = StandaloneTable.gameFromJson(StandaloneTable.gameToJson(running))
        assertEquals(running.players.map { it.investigator }, restored.players.map { it.investigator })
        assertEquals(running.acted, restored.acted)
        assertEquals(running.round, restored.round)
        assertEquals("Sister Beth", running.reset().players[1].investigator)
    }

    @Test fun `stress counts from zero beside the wounds and is saved`() {
        val g = game()
        assertSame(g, g.changeCounter(0, StandaloneGame.STRESS_KIND, -1))
        val stressed = g.changeCounter(0, StandaloneGame.STRESS_KIND, 1).changeCounter(0, StandaloneGame.STRESS_KIND, 1)
        assertEquals(2, stressed.players[0].stress)
        assertEquals(2, stressed.toSummary(0, 0).players[0].stress)
        assertEquals(2, StandaloneTable.gameFromJson(StandaloneTable.gameToJson(stressed)).players[0].stress)
        // A rematch starts everyone fresh.
        assertEquals(0, stressed.claimWin(0, 1_000).rematch(2_000, "e", First).players[0].stress)
    }

    @Test fun `the roster lists every box once with no repeated names`() {
        assertEquals(DeathMayDie.all.size, DeathMayDie.all.distinct().size)
        assertEquals("Season 1 (core box)", DeathMayDie.boxOf("Rasputin"))
        assertTrue(DeathMayDie.boxes.all { it.investigators.isNotEmpty() })
        assertTrue(DeathMayDie.all.all { it.length <= StandaloneGame.MAX_NAME })
        // Pictures only for investigators on the roster, from the wiki's image host.
        assertTrue(DeathMayDie.artUrl("Rasputin")!!.startsWith("https://static.wikia.nocookie.net/cthulhu-death-may-die/images/"))
        assertNull(DeathMayDie.artUrl("Nikola Tesla"))
    }
}
