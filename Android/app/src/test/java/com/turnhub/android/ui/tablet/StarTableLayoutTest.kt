package com.turnhub.android.ui.tablet

import org.junit.Assert.assertEquals
import org.junit.Test

class StarTableLayoutTest {
    @Test fun `seats run clockwise from the top, each facing its own edge`() {
        val top = starSpot(0)
        assertEquals(0f, top.x, 1e-4f)
        assertEquals(-1f, top.y, 1e-4f)
        assertEquals(-180, top.degrees)
        // Seat 2 is to the right of seat 1, as in the setup's star.
        assertEquals(true, starSpot(1).x > 0)
        assertEquals(listOf(-180, -108, -36, 36, 108), (0 until 5).map { starSpot(it).degrees })
    }
}
