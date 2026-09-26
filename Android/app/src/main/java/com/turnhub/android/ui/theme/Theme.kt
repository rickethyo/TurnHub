package com.turnhub.android.ui.theme

import android.app.UiModeManager
import android.content.Context
import android.os.Build
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.platform.LocalContext

/**
 * App-wide theme: the portal's look. [choice] is the player's saved theme;
 * when the device's contrast setting is raised (Android 14+), High contrast
 * replaces it, as the portal does for `prefers-contrast`.
 */
@Composable
fun TurnHubTheme(
    choice: TurnHubThemeChoice = TurnHubThemeChoice.BRASS,
    content: @Composable () -> Unit,
) {
    val context = LocalContext.current
    val highContrast = rememberHighContrast(context)
    val palette = TurnHubPalette.of(if (highContrast) TurnHubThemeChoice.CONTRAST else choice)
    CompositionLocalProvider(LocalTurnHubPalette provides palette) {
        MaterialTheme(
            colorScheme = palette.toColorScheme(),
            typography = turnHubTypography(palette.serifDisplay),
            content = content,
        )
    }
}

/** The active TurnHub tokens. */
val palette: TurnHubPalette
    @Composable get() = LocalTurnHubPalette.current

/** True while the system contrast level is above standard; follows changes live. */
@Composable
private fun rememberHighContrast(context: Context): Boolean {
    if (Build.VERSION.SDK_INT < Build.VERSION_CODES.UPSIDE_DOWN_CAKE) return false
    val uiModeManager = remember(context) { context.getSystemService(UiModeManager::class.java) }
        ?: return false
    var contrast by remember(uiModeManager) { mutableStateOf(uiModeManager.contrast) }
    DisposableEffect(uiModeManager) {
        val listener = UiModeManager.ContrastChangeListener { contrast = it }
        uiModeManager.addContrastChangeListener(context.mainExecutor, listener)
        onDispose { uiModeManager.removeContrastChangeListener(listener) }
    }
    return isHighContrast(contrast)
}

/** Android reports contrast from -1 (reduced) through 0 (standard) to 1 (high); medium is 0.5. */
internal fun isHighContrast(contrast: Float): Boolean = contrast > 0f
