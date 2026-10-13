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
        val faces = (1..2_000).map { TableTools.roll(20, random).substringAfter(": ").toInt() }.toSet()
        assertEquals((1..20).toSet(), faces)
        assertTrue(TableTools.roll(6, random).startsWith("d6: "))
    }

    @Test
    fun `coin and player pick use only real choices`() {
        val random = Random(3)
        assertEquals(setOf("Heads", "Tails"), (1..200).map { TableTools.flip(random) }.toSet())
        assertEquals(setOf("Ana", "Ben"), (1..200).mapNotNull { TableTools.pick(listOf("Ana", "Ben"), random) }.toSet())
        assertNull(TableTools.pick(emptyList(), random))
    }
}
