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
    fun `saved theme keys map onto the V1 themes`() {
        assertEquals(TurnHubThemeChoice.AUTO, TurnHubThemeChoice.fromKey(null))
        assertEquals(TurnHubThemeChoice.AUTO, TurnHubThemeChoice.fromKey("nonsense"))
        assertEquals(TurnHubThemeChoice.GRAPHITE, TurnHubThemeChoice.fromKey("midnight"))
        assertEquals(TurnHubThemeChoice.DAYLIGHT, TurnHubThemeChoice.fromKey("parchment"))
        TurnHubThemeChoice.entries.forEach { assertEquals(it, TurnHubThemeChoice.fromKey(it.key)) }
    }

    @Test
    fun `automatic follows the device and the others are fixed`() {
        assertEquals(TokenTheme.GRAPHITE, TurnHubThemeChoice.AUTO.resolve(systemDark = true))
        assertEquals(TokenTheme.DAYLIGHT, TurnHubThemeChoice.AUTO.resolve(systemDark = false))
        assertEquals(TokenTheme.BRASS, TurnHubThemeChoice.BRASS.resolve(systemDark = false))
        assertEquals(TokenTheme.DAYLIGHT, TurnHubThemeChoice.DAYLIGHT.resolve(systemDark = true))
    }

    @Test
    fun `only Brass draws ornament`() {
        TokenTheme.entries.forEach { theme ->
            assertEquals(theme == TokenTheme.BRASS, TurnHubPalette.of(theme).ornament)
        }
        assertEquals(TokenTheme.GRAPHITE.colors.turn, TurnHubPalette.Graphite.active)
    }

    @Test
    fun `hold times are spoken as seconds`() {
        assertEquals("1 second", secondsLabel(1000))
        assertEquals("2.5 seconds", secondsLabel(2500))
        assertEquals("3.25 seconds", secondsLabel(3250))
        assertEquals("10 seconds", secondsLabel(10000))
    }
}
