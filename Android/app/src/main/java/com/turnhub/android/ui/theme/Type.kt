package com.turnhub.android.ui.theme

import androidx.compose.material3.Typography
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.Font
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontVariation
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.LineHeightStyle
import com.turnhub.android.R

/** One weight of a variable font (res/font, copied from design/fonts by build_tokens.py). */
private fun variable(res: Int, weight: Int) = Font(
    res,
    FontWeight(weight),
    variationSettings = FontVariation.Settings(FontVariation.weight(weight)),
)

private val WEIGHTS = listOf(400, 500, 600, 650, 700, 750, 800)

/** Inter: every text in the modern themes, and all running text in Brass. */
val Inter = FontFamily(WEIGHTS.map { variable(R.font.inter_variable, it) })

/** Cinzel: Brass headings only, as on the Atlas screen and the portal's Brass theme. */
val Cinzel = FontFamily(listOf(400, 500, 600, 700, 800).map { variable(R.font.cinzel_variable, it) })

/** Tabular figures, for clocks and life totals that must not jitter as digits change. */
const val TABULAR = "tnum"

/** A shared type-scale step (DesignTokens.Type) as a Compose style. */
fun TokenTextStyle.toTextStyle(family: FontFamily = Inter): TextStyle = TextStyle(
    fontFamily = family,
    fontSize = size,
    lineHeight = lineHeight,
    fontWeight = weight,
    letterSpacing = tracking,
    fontFeatureSettings = if (tabular) TABULAR else null,
    lineHeightStyle = LineHeightStyle(LineHeightStyle.Alignment.Center, LineHeightStyle.Trim.None),
)

/**
 * Material's roles mapped onto the shared scale (design/tokens.json), set in
 * Inter. Brass ([serifDisplay]) sets display and headings in Cinzel.
 */
fun turnHubTypography(serifDisplay: Boolean): Typography {
    val t = DesignTokens.Type
    val display = if (serifDisplay) Cinzel else Inter
    fun TokenTextStyle.heading() = toTextStyle(display)
    return Typography(
        displayLarge = t.mega.heading().copy(fontFeatureSettings = TABULAR),
        displayMedium = t.hero.heading().copy(fontFeatureSettings = TABULAR),
        displaySmall = t.clock.heading().copy(fontFeatureSettings = TABULAR),
        headlineLarge = t.largeTitle.heading(),
        headlineMedium = t.title1.heading(),
        headlineSmall = t.title2.heading(),
        titleLarge = t.title3.heading(),
        titleMedium = t.headline.heading(),
        titleSmall = t.subhead.toTextStyle().copy(fontWeight = FontWeight(600)),
        bodyLarge = t.body.toTextStyle(),
        bodyMedium = t.subhead.toTextStyle(),
        bodySmall = t.footnote.toTextStyle(),
        labelLarge = t.subhead.toTextStyle().copy(fontWeight = FontWeight(600)),
        labelMedium = t.caption.toTextStyle().copy(fontWeight = FontWeight(600)),
        labelSmall = t.caption.toTextStyle(),
    )
}

/** Kept for callers that want the default scale. */
val TurnHubTypography = turnHubTypography(serifDisplay = false)
