package com.turnhub.android.ui.tablet

import org.junit.Assert.assertEquals
import org.junit.Test

class PublicDisplayTest {
    @Test fun `upright ignores every seat and flip while facing mode retains orientation`() {
        for (seat in listOf(0, 90, 180, 270, -90)) for (turn in listOf(0, 180)) {
            assertEquals(0, publicPanelDegrees(true, seat, turn))
            assertEquals(Math.floorMod(seat + turn, 360), publicPanelDegrees(false, seat, turn))
        }
    }
}
