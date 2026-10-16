package com.turnhub.android.standalone

import com.turnhub.android.domain.PokemonAction
import com.turnhub.android.domain.SpecialCondition
import com.turnhub.android.protocol.GameProfile
import com.turnhub.android.protocol.TableState
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertSame
import org.junit.Assert.assertTrue
import org.junit.Test
import kotlin.random.Random

class PokemonTest {
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
        val energy = StandaloneGame.ACTION_KIND + PokemonAction.ENERGY.name
        assertSame(g, g.changeCounter(1, energy, 1))
        val marked = g.changeCounter(0, energy, 1)
        assertEquals(setOf(PokemonAction.ENERGY), marked.players[0].pokemon.turnActions)
        assertSame(marked, marked.changeCounter(0, StandaloneGame.ACTION_KIND + "MEGA", 1))
        val passed = marked.pass(0, 1_000, Coins(true))
        assertTrue(passed.players.all { it.pokemon.turnActions.isEmpty() })
        assertTrue(passed.pokemonCheckup.isEmpty())
        // Magic counters don't apply.
        assertSame(g, g.changeCounter(0, "poison", 1))
    }

    @Test fun `asleep confused and paralyzed replace each other, burned and poisoned stack`() {
        val g = game().condition(1, SpecialCondition.ASLEEP).condition(1, SpecialCondition.POISONED)
            .condition(1, SpecialCondition.CONFUSED).condition(1, SpecialCondition.BURNED)
        assertEquals(setOf(SpecialCondition.CONFUSED, SpecialCondition.POISONED, SpecialCondition.BURNED), g.players[1].pokemon.conditions)
        assertEquals(setOf(SpecialCondition.POISONED, SpecialCondition.BURNED),
            g.condition(1, SpecialCondition.CONFUSED, false).players[1].pokemon.conditions)
    }

    @Test fun `checkup flips burned and asleep and ends paralysis after its owner's turn`() {
        val g = game().condition(0, SpecialCondition.PARALYZED).condition(0, SpecialCondition.POISONED)
            .condition(1, SpecialCondition.BURNED).condition(1, SpecialCondition.PARALYZED)
        val tails = g.pass(0, 1_000, Coins(false))
        assertEquals(setOf(SpecialCondition.POISONED), tails.players[0].pokemon.conditions)
        assertEquals(setOf(SpecialCondition.BURNED, SpecialCondition.PARALYZED), tails.players[1].pokemon.conditions)
        assertEquals(3, tails.pokemonCheckup.size)
        assertTrue(tails.pokemonCheckup[2].contains("tails"))
        val dismissed = tails.dismissCheckup()
        assertTrue(dismissed.pokemonCheckup.isEmpty())
        assertSame(dismissed, dismissed.dismissCheckup())

        val asleep = game().condition(1, SpecialCondition.ASLEEP).condition(1, SpecialCondition.BURNED)
        val heads = asleep.pass(0, 1_000, Coins(true))
        assertTrue(heads.players[1].pokemon.conditions.isEmpty())
        assertTrue(heads.pokemonCheckup.all { it.contains("heads") })
    }

    @Test fun `pokemon state survives saving and its games are never sent to Atlas`() {
        val g = game().condition(1, SpecialCondition.POISONED)
            .changeCounter(0, StandaloneGame.ACTION_KIND + PokemonAction.SUPPORTER.name, 1)
            .condition(0, SpecialCondition.BURNED).pass(0, 1_000, Coins(false))
            .changeCounter(1, StandaloneGame.ACTION_KIND + PokemonAction.RETREAT.name, 1)
        val restored = StandaloneTable.gameFromJson(StandaloneTable.gameToJson(g))
        assertEquals(g.players.map { it.pokemon }, restored.players.map { it.pokemon })
        assertEquals(g.pokemonCheckup, restored.pokemonCheckup)
        val summary = g.toSummary(0, 0)
        assertEquals(g.pokemonCheckup, summary.pokemonCheckup)
        assertEquals(g.players[1].pokemon, summary.players[1].pokemon)
    }
}
