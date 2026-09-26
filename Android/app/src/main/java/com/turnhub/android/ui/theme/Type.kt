package com.turnhub.android.ui.theme

import androidx.compose.material3.Typography
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight

/**
 * Material's scale with the portal's pairing: a book serif for display and
 * headings (Brass, Parchment), the system sans for everything you read.
 */
fun turnHubTypography(serifDisplay: Boolean): Typography {
    val base = Typography()
    val display = if (serifDisplay) FontFamily.Serif else FontFamily.Default
    fun TextStyle.heading() = copy(fontFamily = display, fontWeight = FontWeight.Bold)
    return base.copy(
        displayLarge = base.displayLarge.heading(),
        displayMedium = base.displayMedium.heading(),
        displaySmall = base.displaySmall.heading(),
        headlineLarge = base.headlineLarge.heading(),
        headlineMedium = base.headlineMedium.heading(),
        headlineSmall = base.headlineSmall.heading(),
        titleLarge = base.titleLarge.heading(),
        titleMedium = base.titleMedium.copy(fontFamily = display, fontWeight = FontWeight.SemiBold),
    )
}

/** Kept for callers that want the default scale. */
val TurnHubTypography = turnHubTypography(serifDisplay = true)
