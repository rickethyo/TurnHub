package com.turnhub.android.standalone

import com.turnhub.android.protocol.GameProfile
import com.turnhub.android.protocol.TableState
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import kotlin.random.Random

/** Engine details the shared scenarios don't cover: setup, randomness and the shuffled deck. */
class PlanechaseTest {
    private fun lobby(profile: GameProfile = GameProfile.MTG_COMMANDER, players: Int = 4): StandaloneGame {
        var game = StandaloneGame().setFormat(profile)
        repeat(players) { game = game.addPlayer("P${it + 1}") }
        return game
    }

    private val pool = (1..50).map { PlanarCard("plane-$it") } + (1..21).map { PlanarCard("phen-$it", phenomenon = true) }

    @Test fun `planechase is a Magic-only lobby switch that survives a return to the lobby`() {
        assertFalse(lobby(GameProfile.YUGIOH, 2).setPlanechase(true).planechase.enabled)
        val on = lobby().setPlanechase(true)
        assertTrue(on.planechase.enabled)
        // Leaving Magic turns it off.
        assertFalse(on.setFormat(GameProfile.GENERIC).planechase.enabled)
        val started = on.start(0, "g", Random(1), Planechase.shuffledDeck(pool, 4, true, Random(2)))
        assertTrue(started.planechase.faceUp.isNotEmpty())
        // A running game takes no setup changes.
        assertTrue(started.setPlanechase(false) === started)
        val back = started.reset()
        assertTrue(back.planechase.enabled)
        assertTrue(back.planechase.deck.isEmpty() && back.planechase.faceUp.isEmpty())
    }

    @Test fun `a game without planechase deals no planes and refuses planar actions`() {
        val game = lobby().start(0, "g", Random(1), Planechase.shuffledDeck(pool, 4, true, Random(2)))
        assertTrue(game.planechase.faceUp.isEmpty())
        assertTrue(game.rollPlanarDie(game.active!!, PlanarFace.BLANK) === game)
    }

    @Test fun `a digital game started without downloaded planes falls back to the players' own cards`() {
        val game = lobby().setPlanechase(true).start(0, "g", Random(1), emptyList())
        assertEquals(PlanarDeckSource.DIGITAL, game.planechase.source)
        assertTrue(game.planechase.physical)
        val next = game.changePlaneCounter(0, Planechase.PHYSICAL_PLANE, 1)
        assertEquals(mapOf(Planechase.PHYSICAL_PLANE to 1), next.planechase.counters)
    }

    @Test fun `the shared deck has every plane and at most two phenomena per player`() {
        val deck = Planechase.shuffledDeck(pool, 3, phenomena = true, Random(7))
        assertEquals(50, deck.count { !it.phenomenon })
        assertEquals(6, deck.count { it.phenomenon })
        assertEquals(deck.size, deck.distinctBy { it.id }.size)
        assertEquals(0, Planechase.shuffledDeck(pool, 3, phenomena = false, Random(7)).count { it.phenomenon })
    }

    @Test fun `the planar die is fair`() {
        val random = Random(42)
        val faces = List(60_000) { PlanarFace.roll(random) }.groupingBy { it }.eachCount()
        assertEquals(10_000.0, faces.getValue(PlanarFace.PLANESWALK).toDouble(), 400.0)
        assertEquals(10_000.0, faces.getValue(PlanarFace.CHAOS).toDouble(), 400.0)
        assertEquals(40_000.0, faces.getValue(PlanarFace.BLANK).toDouble(), 600.0)
    }

    @Test fun `a deck of only phenomena deals no starting plane instead of looping`() {
        val game = lobby().setPlanechase(true).start(0, "g", Random(1), listOf(PlanarCard("x", true), PlanarCard("y", true)))
        assertEquals(TableState.RUNNING, game.state)
        assertTrue(game.planechase.faceUp.isEmpty())
    }

    @Test fun `the table rolls for the active player and saves the result`() {
        val store = MemoryStandaloneStore(lobby().setPlanechase(true), LocalLibrary())
        val table = StandaloneTable(store, planarPool = { pool }, planarRandom = Random(3))
        table.start()
        val started = table.state.value.game
        assertTrue(started.planechase.faceUp.isNotEmpty())
        table.rollPlanarDie()
        val rolled = table.state.value.game.planechase
        assertEquals(started.active, rolled.lastRoll?.player)
        assertEquals(1, rolled.cost(started.active!!))
        // Reloading the store gives the same game back.
        assertEquals(table.state.value.game, StandaloneTable(store).state.value.game)
    }
}
