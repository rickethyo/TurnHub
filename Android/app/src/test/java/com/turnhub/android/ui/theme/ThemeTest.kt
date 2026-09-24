package com.turnhub.android.ui.theme

import com.turnhub.android.ui.home.secondsLabel
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class ThemeTest {
    @Test
    fun `medium and high system contrast switch to the high-contrast scheme`() {
        assertFalse(isHighContrast(-1f))
        assertFalse(isHighContrast(0f))
        assertTrue(isHighContrast(0.5f))
        assertTrue(isHighContrast(1f))
    }

    @Test
    fun `hold times are spoken as seconds`() {
        assertEquals("1 second", secondsLabel(1000))
        assertEquals("2.5 seconds", secondsLabel(2500))
        assertEquals("3.25 seconds", secondsLabel(3250))
        assertEquals("10 seconds", secondsLabel(10000))
    }
}
