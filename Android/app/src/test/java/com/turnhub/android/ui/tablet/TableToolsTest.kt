package com.turnhub.android.ui.tablet

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import kotlin.random.Random

class TableToolsTest {
    @Test
    fun `dice stay in range and cover every face`() {
        val random = Random(7)
        TableTools.dice.forEach { sides ->
            val faces = (1..2_000).map { TableTools.roll(sides, random).substringAfter(": ").toInt() }.toSet()
            assertEquals((1..sides).toSet(), faces)
        }
        assertTrue(TableTools.roll(6, random).startsWith("d6: "))
    }

    @Test fun `opponents exclude self eliminated teammates and Star allies by seat not name`() {
        val game = com.turnhub.android.standalone.StandaloneGame(
            players = List(5) { com.turnhub.android.standalone.LocalPlayer("Same name", eliminated = it == 3) })
        assertEquals(listOf(2, 3, 5), TableTools.opponents(game.toSummary(0, 0), 1).map { it.playerNumber })
        val teams = game.copy(twoHeadedGiant = true)
        assertEquals(listOf(3, 5), TableTools.opponents(teams.toSummary(0, 0), 1).map { it.playerNumber })
        val star = game.copy(profile = com.turnhub.android.protocol.GameProfile.MTG_STAR_COMMANDER)
        assertEquals(listOf(3), TableTools.opponents(star.toSummary(0, 0), 1).map { it.playerNumber })
        assertTrue(TableTools.opponents(star.toSummary(0, 0), 4).isEmpty())
        assertTrue(TableTools.opponents(star.toSummary(0, 0), 99).isEmpty())
    }

    @Test
    fun `coin and player pick use only real choices`() {
        val random = Random(3)
        assertEquals(setOf("Heads", "Tails"), (1..200).map { TableTools.flip(random) }.toSet())
        assertEquals(setOf("Ana", "Ben"), (1..200).mapNotNull { TableTools.pick(listOf("Ana", "Ben"), random) }.toSet())
        assertNull(TableTools.pick(emptyList(), random))
    }
}
