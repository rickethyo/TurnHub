package com.turnhub.android.ui.theme

import androidx.compose.ui.graphics.Color

// Placeholder brand palette -- no product mark/branding exists yet. Swap these
// once TurnHub has real brand colors; nothing else in the app should need to
// change to pick up new ones.
val TurnHubBlue40 = Color(0xFF2F5FA8)
val TurnHubBlue80 = Color(0xFFA8C7FA)

// High-contrast schemes, used instead of wallpaper colours when the device's
// contrast setting is raised. Text and controls meet at least 7:1 against
// their backgrounds; outlines are full-strength so controls stay visible.
val HighContrastLight = androidx.compose.material3.lightColorScheme(
    primary = Color(0xFF002F6C),
    onPrimary = Color.White,
    primaryContainer = Color(0xFF002F6C),
    onPrimaryContainer = Color.White,
    secondary = Color(0xFF1A1A1A),
    onSecondary = Color.White,
    tertiaryContainer = Color(0xFF1A1A1A),
    onTertiaryContainer = Color.White,
    background = Color.White,
    onBackground = Color.Black,
    surface = Color.White,
    onSurface = Color.Black,
    surfaceVariant = Color(0xFFF0F0F0),
    onSurfaceVariant = Color.Black,
    outline = Color.Black,
    error = Color(0xFF8C0009),
    onError = Color.White,
)

val HighContrastDark = androidx.compose.material3.darkColorScheme(
    primary = Color(0xFFFFD400),
    onPrimary = Color.Black,
    primaryContainer = Color(0xFFFFD400),
    onPrimaryContainer = Color.Black,
    secondary = Color.White,
    onSecondary = Color.Black,
    tertiaryContainer = Color.White,
    onTertiaryContainer = Color.Black,
    background = Color.Black,
    onBackground = Color.White,
    surface = Color.Black,
    onSurface = Color.White,
    surfaceVariant = Color(0xFF1A1A1A),
    onSurfaceVariant = Color.White,
    outline = Color.White,
    error = Color(0xFFFFB4AB),
    onError = Color.Black,
)
