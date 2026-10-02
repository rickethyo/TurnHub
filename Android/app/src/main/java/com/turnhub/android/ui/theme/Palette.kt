package com.turnhub.android.ui.theme

import androidx.compose.material3.ColorScheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Immutable
import androidx.compose.runtime.staticCompositionLocalOf
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.SolidColor

/**
 * The appearance choices, matching the portal's (design/tokens.json). A theme
 * is only how this phone looks; it never affects game state. The choice is
 * saved on the phone, like the portal's per-browser choice. [AUTO] follows the
 * device: Graphite when it is dark, Daylight when it is light.
 */
enum class TurnHubThemeChoice(val key: String, val label: String, val blurb: String, val token: TokenTheme?) {
    AUTO("auto", "Automatic", "Graphite or Daylight, following your phone's dark theme.", null),
    GRAPHITE("graphite", TokenTheme.GRAPHITE.label, TokenTheme.GRAPHITE.blurb, TokenTheme.GRAPHITE),
    DAYLIGHT("daylight", TokenTheme.DAYLIGHT.label, TokenTheme.DAYLIGHT.blurb, TokenTheme.DAYLIGHT),
    BRASS("brass", TokenTheme.BRASS.label, TokenTheme.BRASS.blurb, TokenTheme.BRASS),
    CONTRAST("contrast", TokenTheme.CONTRAST.label, TokenTheme.CONTRAST.blurb, TokenTheme.CONTRAST),
    ;

    /** The theme actually drawn, given the device's dark setting. */
    fun resolve(systemDark: Boolean): TokenTheme =
        token ?: if (systemDark) TokenTheme.DEFAULT_DARK else TokenTheme.DEFAULT_LIGHT

    companion object {
        fun fromKey(key: String?): TurnHubThemeChoice = when (key) {
            // Names from before V1: the modern dark and light themes replace them.
            "midnight" -> GRAPHITE
            "parchment" -> DAYLIGHT
            else -> entries.firstOrNull { it.key == key } ?: AUTO
        }
    }
}

/** Semantic tokens the TurnHub components draw with, beyond Material's roles. */
@Immutable
data class TurnHubPalette(
    val dark: Boolean,
    val bg: Color,
    val glowA: Color,
    val glowB: Color,
    val surface: Color,
    val surface2: Color,
    val surface3: Color,
    val inset: Color,
    val line: Color,
    val lineStrong: Color,
    val text: Color,
    val muted: Color,
    val faint: Color,
    val accent: Color,
    val accentHi: Color,
    val accentLo: Color,
    val onAccent: Color,
    val good: Color,
    val warn: Color,
    val bad: Color,
    val info: Color,
    val active: Color,
    val onActive: Color,
    val activeSoft: Color,
    val accentSoft: Color,
    val fill: Color,
    val fillStrong: Color,
    val scrim: Color,
    val focus: Color,
    /** Brass only: sheens, rivets, gauges and gradients. The modern themes draw flat. */
    val ornament: Boolean,
    val rivets: Boolean,
    val serifDisplay: Boolean,
    /** Avatar background saturation/lightness, as the portal's --av-s / --av-l. */
    val avatarSaturation: Float,
    val avatarLightness: Float,
    val avatarText: Color,
) {
    val accentBrush: Brush
        get() = if (accentHi == accentLo) SolidColor(accent)
        else Brush.verticalGradient(listOf(accentHi, accent, accentLo))

    /** The same eight decorative hues the portal gives players. The name is always shown too. */
    fun avatarColor(playerNumber: Int): Color {
        val hue = AVATAR_HUES[Math.floorMod(playerNumber, AVATAR_HUES.size)].toFloat()
        return Color.hsl(hue, avatarSaturation, avatarLightness)
    }

    companion object {
        private val AVATAR_HUES = intArrayOf(38, 168, 12, 205, 95, 280, 330, 60)

        /** Every role from the shared tokens; Brass keeps its gold gradient for ornament. */
        fun of(theme: TokenTheme): TurnHubPalette {
            val c = theme.colors
            val brass = theme.brassOrnament
            val contrast = theme == TokenTheme.CONTRAST
            return TurnHubPalette(
                dark = theme.dark,
                bg = c.bg, glowA = c.glowA, glowB = c.glowB,
                surface = c.surface1, surface2 = c.surface2, surface3 = c.surface3,
                inset = if (theme.dark) c.bg else c.surface2,
                line = c.separator, lineStrong = c.separatorStrong,
                text = c.text, muted = c.textSecondary, faint = c.textTertiary,
                accent = c.accent,
                accentHi = if (brass) Color(0xFFF8D98F) else c.accent,
                accentLo = if (brass) Color(0xFF9A681D) else c.accent,
                onAccent = c.onAccent,
                good = c.good, warn = c.warning, bad = c.critical, info = c.info,
                active = c.turn, onActive = c.onTurn, activeSoft = c.turnSoft,
                accentSoft = c.accentSoft, fill = c.fill, fillStrong = c.fillStrong,
                scrim = c.scrim, focus = c.focus,
                ornament = brass, rivets = brass, serifDisplay = theme.serifDisplay,
                avatarSaturation = when {
                    contrast -> 0f
                    brass -> .42f
                    else -> .5f
                },
                avatarLightness = when {
                    contrast -> .18f
                    brass -> .34f
                    theme.dark -> .38f
                    else -> .44f
                },
                avatarText = if (brass) Color(0xFFFFF6E4) else Color.White,
            )
        }

        val Graphite = of(TokenTheme.GRAPHITE)
        val Daylight = of(TokenTheme.DAYLIGHT)
        val Brass = of(TokenTheme.BRASS)
        val Contrast = of(TokenTheme.CONTRAST)
    }
}

val LocalTurnHubPalette = staticCompositionLocalOf { TurnHubPalette.Graphite }

internal fun TurnHubPalette.toColorScheme(): ColorScheme {
    val base = if (dark) darkColorScheme() else lightColorScheme()
    return base.copy(
        primary = accent,
        onPrimary = onAccent,
        primaryContainer = accentSoft,
        onPrimaryContainer = if (dark) accentHi else accent,
        secondary = active,
        onSecondary = onActive,
        secondaryContainer = activeSoft,
        onSecondaryContainer = text,
        tertiary = info,
        tertiaryContainer = surface3,
        onTertiaryContainer = text,
        background = bg,
        onBackground = text,
        surface = surface,
        onSurface = text,
        surfaceVariant = surface2,
        onSurfaceVariant = muted,
        surfaceContainerLowest = inset,
        surfaceContainerLow = surface,
        surfaceContainer = surface2,
        surfaceContainerHigh = surface2,
        surfaceContainerHighest = surface3,
        outline = lineStrong,
        outlineVariant = line,
        scrim = scrim,
        error = bad,
        onError = if (dark) Color.Black else Color.White,
        errorContainer = surface3,
        onErrorContainer = bad,
    )
}
