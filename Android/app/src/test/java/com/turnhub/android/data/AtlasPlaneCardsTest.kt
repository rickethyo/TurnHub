package com.turnhub.android.data

import org.junit.Assert.assertEquals
import org.junit.Test

class AtlasPlaneCardsTest {
    private fun plane(text: String) = PlaneInfo("id", "Made-up Plane", "Plane — Nowhere", text, "A. Painter", "https://x", "Set")

    @Test fun chaosAbilityIsKeptApartAndTextIsPlainAscii() {
        val card = AtlasPlaneCards.text(plane("Creatures get +1/+1.\nYou can’t lose.\nWhenever you roll {CHAOS}, draw a card."))
        assertEquals("Plane - Nowhere", card.type)
        assertEquals("Creatures get +1/+1.\nYou can't lose.", card.text)
        assertEquals("Whenever you roll chaos, draw a card.", card.chaos)
        assertEquals("A. Painter", card.artist)
    }

    @Test fun phenomenaHaveNoChaosLine() {
        val card = AtlasPlaneCards.text(plane("When you encounter this, each player draws a card."))
        assertEquals("", card.chaos)
        assertEquals("When you encounter this, each player draws a card.", card.text)
    }
}
