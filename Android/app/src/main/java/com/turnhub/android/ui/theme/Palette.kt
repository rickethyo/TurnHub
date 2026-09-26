package com.turnhub.android.ui.theme

import androidx.compose.material3.ColorScheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Immutable
import androidx.compose.runtime.staticCompositionLocalOf
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color

/**
 * The portal's themes (Atlas/src/web_pages.cpp, THEME_CSS), as Compose tokens.
 * A theme is only how this phone looks; it never affects game state. The
 * choice is saved on the phone (like the portal's per-browser choice).
 */
enum class TurnHubThemeChoice(val key: String, val label: String, val blurb: String) {
    BRASS("brass", "Brass", "Steampunk gold, rivets and gauges."),
    MIDNIGHT("midnight", "Midnight", "Clean modern dark with gold."),
    PARCHMENT("parchment", "Parchment", "Light sepia for bright rooms."),
    CONTRAST("contrast", "High contrast", "Maximum legibility. Chosen automatically when your device asks for more contrast."),
    ;

    companion object {
        fun fromKey(key: String?): TurnHubThemeChoice = entries.firstOrNull { it.key == key } ?: BRASS
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
    val rivets: Boolean,
    val serifDisplay: Boolean,
    /** Avatar background saturation/lightness, as the portal's --av-s / --av-l. */
    val avatarSaturation: Float,
    val avatarLightness: Float,
    val avatarText: Color,
) {
    val accentBrush: Brush
        get() = if (accentHi == accentLo) Brush.verticalGradient(listOf(accent, accent))
        else Brush.verticalGradient(listOf(accentHi, accent, accentLo))

    /** The same eight decorative hues the portal gives players. The name is always shown too. */
    fun avatarColor(playerNumber: Int): Color {
        val hue = AVATAR_HUES[Math.floorMod(playerNumber, AVATAR_HUES.size)].toFloat()
        return Color.hsl(hue, avatarSaturation, avatarLightness)
    }

    companion object {
        private val AVATAR_HUES = intArrayOf(38, 168, 12, 205, 95, 280, 330, 60)

        fun of(choice: TurnHubThemeChoice): TurnHubPalette = when (choice) {
            TurnHubThemeChoice.BRASS -> Brass
            TurnHubThemeChoice.MIDNIGHT -> Midnight
            TurnHubThemeChoice.PARCHMENT -> Parchment
            TurnHubThemeChoice.CONTRAST -> Contrast
        }

        val Brass = TurnHubPalette(
            dark = true,
            bg = Color(0xFF110D09), glowA = Color(0x2BE0A848), glowB = Color(0x0F5FD4B0),
            surface = Color(0xFF1B1510), surface2 = Color(0xFF241C14), surface3 = Color(0xFF30251A), inset = Color(0xFF0C0906),
            line = Color(0x33DEB064), lineStrong = Color(0x70DEB064),
            text = Color(0xFFF6ECD9), muted = Color(0xFFC9B594), faint = Color(0xFF97866B),
            accent = Color(0xFFE2AE4A), accentHi = Color(0xFFF8D98F), accentLo = Color(0xFF9A681D), onAccent = Color(0xFF1C1205),
            good = Color(0xFF93DC8C), warn = Color(0xFFFFA25C), bad = Color(0xFFFF7D70), info = Color(0xFFA6C8EA), active = Color(0xFF62D6B2),
            rivets = true, serifDisplay = true, avatarSaturation = .42f, avatarLightness = .34f, avatarText = Color(0xFFFFF6E4),
        )
        val Midnight = TurnHubPalette(
            dark = true,
            bg = Color(0xFF0A0D14), glowA = Color(0x1AE8B44F), glowB = Color(0x1A588CFF),
            surface = Color(0xFF121826), surface2 = Color(0xFF182033), surface3 = Color(0xFF202A40), inset = Color(0xFF0A0F1A),
            line = Color(0x26A0B6E0), lineStrong = Color(0x4DA0B6E0),
            text = Color(0xFFEEF2F9), muted = Color(0xFFA8B3C9), faint = Color(0xFF76819A),
            accent = Color(0xFFE8B44F), accentHi = Color(0xFFFFD57F), accentLo = Color(0xFFE3A73D), onAccent = Color(0xFF171003),
            good = Color(0xFF72E3A2), warn = Color(0xFFFFB25A), bad = Color(0xFFFF7D88), info = Color(0xFF94B9FF), active = Color(0xFF5AD8CA),
            rivets = false, serifDisplay = false, avatarSaturation = .48f, avatarLightness = .38f, avatarText = Color.White,
        )
        val Parchment = TurnHubPalette(
            dark = false,
            bg = Color(0xFFECE2CD), glowA = Color(0x38C49234), glowB = Color(0x143C826E),
            surface = Color(0xFFFBF6EA), surface2 = Color(0xFFF3E9D4), surface3 = Color(0xFFE9DBBD), inset = Color(0xFFFFFDF7),
            line = Color(0x3870501C), lineStrong = Color(0x7370501C),
            text = Color(0xFF2A1E10), muted = Color(0xFF634F33), faint = Color(0xFF806A4A),
            accent = Color(0xFF8E5F0C), accentHi = Color(0xFFC99534), accentLo = Color(0xFF8A5C0D), onAccent = Color(0xFFFFFAF0),
            good = Color(0xFF1F6F3A), warn = Color(0xFF9C4700), bad = Color(0xFFAD2319), info = Color(0xFF1F5A8F), active = Color(0xFF0D7560),
            rivets = true, serifDisplay = true, avatarSaturation = .40f, avatarLightness = .40f, avatarText = Color.White,
        )
        val Contrast = TurnHubPalette(
            dark = true,
            bg = Color.Black, glowA = Color.Transparent, glowB = Color.Transparent,
            surface = Color.Black, surface2 = Color(0xFF0E0E0E), surface3 = Color(0xFF1C1C1C), inset = Color.Black,
            line = Color(0xFFBDBDBD), lineStrong = Color.White,
            text = Color.White, muted = Color(0xFFECECEC), faint = Color(0xFFD0D0D0),
            accent = Color(0xFFFFD400), accentHi = Color(0xFFFFD400), accentLo = Color(0xFFFFD400), onAccent = Color.Black,
            good = Color(0xFF7DFF9B), warn = Color(0xFFFFB000), bad = Color(0xFFFF9A9A), info = Color(0xFF8FD3FF), active = Color(0xFF00F0D8),
            rivets = false, serifDisplay = false, avatarSaturation = 0f, avatarLightness = .18f, avatarText = Color.White,
        )
    }
}

val LocalTurnHubPalette = staticCompositionLocalOf { TurnHubPalette.Brass }

internal fun TurnHubPalette.toColorScheme(): ColorScheme {
    val base = if (dark) darkColorScheme() else lightColorScheme()
    return base.copy(
        primary = accent,
        onPrimary = onAccent,
        primaryContainer = surface3,
        onPrimaryContainer = accentHi,
        secondary = active,
        onSecondary = if (dark) Color.Black else Color.White,
        secondaryContainer = surface3,
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
        error = bad,
        onError = if (dark) Color.Black else Color.White,
        errorContainer = surface3,
        onErrorContainer = bad,
    )
}
