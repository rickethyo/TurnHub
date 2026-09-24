package com.turnhub.android.ui.theme

import android.app.UiModeManager
import android.content.Context
import android.os.Build
import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.dynamicDarkColorScheme
import androidx.compose.material3.dynamicLightColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.platform.LocalContext

private val LightColors = lightColorScheme(primary = TurnHubBlue40)
private val DarkColors = darkColorScheme(primary = TurnHubBlue80)

/**
 * App-wide Material3 theme. When the device's contrast setting is raised
 * (Android 14+, Settings > Accessibility > Colour and motion > Contrast), the
 * fixed high-contrast scheme in [Color.kt] replaces everything else. Otherwise
 * dynamic colour (Android 12+) follows the wallpaper, falling back to the
 * placeholder TurnHub palette.
 */
@Composable
fun TurnHubTheme(
    darkTheme: Boolean = isSystemInDarkTheme(),
    dynamicColor: Boolean = true,
    content: @Composable () -> Unit,
) {
    val context = LocalContext.current
    val highContrast = rememberHighContrast(context)
    val colorScheme = when {
        highContrast -> if (darkTheme) HighContrastDark else HighContrastLight
        dynamicColor && Build.VERSION.SDK_INT >= Build.VERSION_CODES.S ->
            if (darkTheme) dynamicDarkColorScheme(context) else dynamicLightColorScheme(context)
        darkTheme -> DarkColors
        else -> LightColors
    }

    MaterialTheme(
        colorScheme = colorScheme,
        typography = TurnHubTypography,
        content = content,
    )
}

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
