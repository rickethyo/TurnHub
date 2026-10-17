package com.turnhub.android.standalone

import com.turnhub.android.domain.TurnAction
import com.turnhub.android.domain.SpecialCondition
import com.turnhub.android.protocol.GameProfile
import com.turnhub.android.protocol.TableState
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertSame
import org.junit.Assert.assertTrue
import org.junit.Test
import kotlin.random.Random

class CardGamesTest {
    /** Every coin comes up as [heads]; with tails, player 1 also starts. */
    private class Coins(private val heads: Boolean) : Random() {
        override fun nextBits(bitCount: Int): Int = if (heads) -1 else 0
    }

    private fun lobby(players: Int = 2) = (1..players).fold(StandaloneGame().setFormat(GameProfile.POKEMON)) { g, i -> g.addPlayer("P$i") }

    private fun game() = lobby().start(0, "g", Coins(false))

    private fun StandaloneGame.condition(index: Int, condition: SpecialCondition, on: Boolean = true) =
        changeCounter(index, StandaloneGame.CONDITION_KIND + condition.name, if (on) 1 else 0)

    @Test fun `pokemon is a two-player game with six prizes, phone-hosted only`() {
        assertEquals(6, lobby().startingLife)
        assertFalse(lobby(3).canStart)
        assertTrue(lobby(3).setHouseRules(true).canStart)
        assertFalse(lobby().setTwoHeadedGiant(true).twoHeadedGiant)
        assertFalse(lobby().setPlanechase(true).planechase.enabled)
        assertFalse(GameProfile.POKEMON.atlasSupported)
        assertEquals(listOf(6, 6), game().players.map { it.life })
    }

    @Test fun `taking the last prize wins and prizes stay between zero and the start`() {
        val g = game()
        assertSame(g, g.changeLife(0, 1))
        val five = g.changeLife(0, -1)
        assertEquals(5, five.players[0].life)
        assertEquals(TableState.RUNNING, g.changeLife(0, -5).state)
        val won = g.changeLife(0, -9)
        assertEquals(TableState.GAME_OVER, won.state)
        assertEquals(listOf(0), won.winners)
        assertEquals(0, won.players[0].life)
        // No life to lose: nobody is eliminated at zero.
        assertFalse(won.players.any { it.eliminated })
    }

    @Test fun `once-per-turn actions belong to the active player and reset on pass`() {
        val g = game()
        val energy = StandaloneGame.ACTION_KIND + TurnAction.ENERGY.name
        assertSame(g, g.changeCounter(1, energy, 1))
        val marked = g.changeCounter(0, energy, 1)
        assertEquals(setOf(TurnAction.ENERGY), marked.players[0].turn.turnActions)
        assertSame(marked, marked.changeCounter(0, StandaloneGame.ACTION_KIND + "MEGA", 1))
        val passed = marked.pass(0, 1_000, Coins(true))
        assertTrue(passed.players.all { it.turn.turnActions.isEmpty() })
        assertTrue(passed.pokemonCheckup.isEmpty())
        // Magic counters don't apply.
        assertSame(g, g.changeCounter(0, "poison", 1))
    }

    @Test fun `asleep confused and paralyzed replace each other, burned and poisoned stack`() {
        val g = game().condition(1, SpecialCondition.ASLEEP).condition(1, SpecialCondition.POISONED)
            .condition(1, SpecialCondition.CONFUSED).condition(1, SpecialCondition.BURNED)
        assertEquals(setOf(SpecialCondition.CONFUSED, SpecialCondition.POISONED, SpecialCondition.BURNED), g.players[1].turn.conditions)
        assertEquals(setOf(SpecialCondition.POISONED, SpecialCondition.BURNED),
            g.condition(1, SpecialCondition.CONFUSED, false).players[1].turn.conditions)
    }

    @Test fun `checkup flips burned and asleep and ends paralysis after its owner's turn`() {
        val g = game().condition(0, SpecialCondition.PARALYZED).condition(0, SpecialCondition.POISONED)
            .condition(1, SpecialCondition.BURNED).condition(1, SpecialCondition.PARALYZED)
        val tails = g.pass(0, 1_000, Coins(false))
        assertEquals(setOf(SpecialCondition.POISONED), tails.players[0].turn.conditions)
        assertEquals(setOf(SpecialCondition.BURNED, SpecialCondition.PARALYZED), tails.players[1].turn.conditions)
        assertEquals(3, tails.pokemonCheckup.size)
        assertTrue(tails.pokemonCheckup[2].contains("tails"))
        val dismissed = tails.dismissCheckup()
        assertTrue(dismissed.pokemonCheckup.isEmpty())
        assertSame(dismissed, dismissed.dismissCheckup())

        val asleep = game().condition(1, SpecialCondition.ASLEEP).condition(1, SpecialCondition.BURNED)
        val heads = asleep.pass(0, 1_000, Coins(true))
        assertTrue(heads.players[1].turn.conditions.isEmpty())
        assertTrue(heads.pokemonCheckup.all { it.contains("heads") })
    }

    @Test fun `pokemon state survives saving and its games are never sent to Atlas`() {
        val g = game().condition(1, SpecialCondition.POISONED)
            .changeCounter(0, StandaloneGame.ACTION_KIND + TurnAction.SUPPORTER.name, 1)
            .condition(0, SpecialCondition.BURNED).pass(0, 1_000, Coins(false))
            .changeCounter(1, StandaloneGame.ACTION_KIND + TurnAction.RETREAT.name, 1)
        val restored = StandaloneTable.gameFromJson(StandaloneTable.gameToJson(g))
        assertEquals(g.players.map { it.turn }, restored.players.map { it.turn })
        assertEquals(g.pokemonCheckup, restored.pokemonCheckup)
        val summary = g.toSummary(0, 0)
        assertEquals(g.pokemonCheckup, summary.pokemonCheckup)
        assertEquals(g.players[1].turn, summary.players[1].turn)
    }

    private fun lorcana(players: Int = 3) =
        (1..players).fold(StandaloneGame().setFormat(GameProfile.LORCANA)) { g, i -> g.addPlayer("P$i") }.start(0, "l", Coins(false))

    @Test fun `lorcana counts lore up from zero and reaching the goal wins at once`() {
        assertEquals(20, StandaloneGame().setFormat(GameProfile.LORCANA).startingLife)
        assertFalse(GameProfile.LORCANA.atlasSupported)
        val g = lorcana()
        assertEquals(listOf(0, 0, 0), g.players.map { it.life })
        assertSame(g, g.changeLife(1, -1))
        val nineteen = g.changeLife(1, 19)
        assertEquals(TableState.RUNNING, nineteen.state)
        // Losing lore never eliminates; it stops at zero.
        assertEquals(0, nineteen.changeLife(0, -3).players[0].life)
        val won = nineteen.changeLife(1, 2)
        assertEquals(TableState.GAME_OVER, won.state)
        assertEquals(listOf(1), won.winners)
        assertEquals(21, won.players[1].life)
    }

    @Test fun `a player who can't draw leaves and the last player left wins`() {
        val g = lorcana().eliminate(0, 1_000)
        assertEquals(TableState.RUNNING, g.state)
        val over = g.eliminate(1, 2_000)
        assertEquals(listOf(2), over.winners)
    }

    @Test fun `lorcana's ink reminder is the active player's and resets on pass`() {
        val g = lorcana(2)
        val ink = StandaloneGame.ACTION_KIND + TurnAction.INK.name
        assertSame(g, g.changeCounter(0, StandaloneGame.ACTION_KIND + TurnAction.ENERGY.name, 1))
        assertSame(g, g.changeCounter(0, StandaloneGame.CONDITION_KIND + SpecialCondition.ASLEEP.name, 1))
        val inked = g.changeCounter(0, ink, 1)
        assertEquals(setOf(TurnAction.INK), inked.players[0].turn.turnActions)
        val passed = inked.pass(0, 1_000)
        assertTrue(passed.players.all { it.turn.turnActions.isEmpty() })
        assertTrue(passed.pokemonCheckup.isEmpty())
        val restored = StandaloneTable.gameFromJson(StandaloneTable.gameToJson(inked))
        assertEquals(inked.players.map { it.turn to it.life }, restored.players.map { it.turn to it.life })
    }
}
