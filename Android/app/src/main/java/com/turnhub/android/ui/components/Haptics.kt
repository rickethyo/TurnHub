package com.turnhub.android.ui.components

import android.content.Context
import android.os.Build
import android.os.VibrationEffect
import android.os.Vibrator
import android.os.VibratorManager
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.platform.LocalContext

/**
 * Firm haptics through the vibrator itself. Compose's view haptics are the
 * light system "touch feedback" ticks, which felt too weak at the table
 * (owner, 2026-10-02). Where the motor supports composed primitives (most
 * phones from Android 11), each event gets a full-strength primitive; older
 * phones get the strongest predefined click or a short full-amplitude pulse.
 * The system's vibration settings still apply, and haptics never carry
 * information on their own: every event also shows on screen.
 */
class TurnHubHaptics internal constructor(private val vibrator: Vibrator?) {

    /** A life button or a tab: one crisp click. */
    fun tick() = play(listOf(Primitive.CLICK to 1f), fallbackMs = 18)

    /** An action went through (Pass): a strong double click. */
    fun confirm() = play(
        listOf(Primitive.CLICK to 1f, Primitive.CLICK to 1f),
        fallbackMs = 35,
    )

    /** Another player nudged this phone's player: a quick triple tap. */
    fun nudged() = play(
        listOf(
            Primitive.CLICK to 1f,
            Primitive.CLICK to 1f,
            Primitive.CLICK to 1f,
        ),
        fallbackMs = 90,
    )

    /** An action was undone (cancel a pass): a low thud. */
    fun reject() = play(listOf(Primitive.THUD to 1f), fallbackMs = 45)

    /** The turn reached this phone's player: unmistakable, felt in a pocket. */
    fun yourTurn() = play(
        listOf(
            Primitive.THUD to 1f,
            Primitive.CLICK to 1f,
            Primitive.CLICK to 1f,
        ),
        fallbackMs = 120,
    )

    private enum class Primitive(val id: Int) {
        CLICK(VibrationEffect.Composition.PRIMITIVE_CLICK),
        THUD(VibrationEffect.Composition.PRIMITIVE_THUD),
    }

    private fun play(primitives: List<Pair<Primitive, Float>>, fallbackMs: Long) {
        val v = vibrator?.takeIf { it.hasVibrator() } ?: return
        val effect = when {
            Build.VERSION.SDK_INT >= Build.VERSION_CODES.R && primitives.map { it.first }.distinct().all { primitive ->
                when (primitive) {
                    Primitive.CLICK -> v.areAllPrimitivesSupported(VibrationEffect.Composition.PRIMITIVE_CLICK)
                    Primitive.THUD -> v.areAllPrimitivesSupported(VibrationEffect.Composition.PRIMITIVE_THUD)
                }
            } ->
                VibrationEffect.startComposition().apply {
                    primitives.forEachIndexed { i, (primitive, scale) -> addPrimitive(primitive.id, scale, if (i == 0) 0 else GAP_MS) }
                }.compose()
            Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q && fallbackMs < 60 ->
                VibrationEffect.createPredefined(VibrationEffect.EFFECT_HEAVY_CLICK)
            else -> VibrationEffect.createOneShot(fallbackMs, 255)
        }
        runCatching { v.vibrate(effect) }
    }

    private companion object {
        const val GAP_MS = 70
    }
}

private fun Context.vibrator(): Vibrator? =
    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
        getSystemService(VibratorManager::class.java)?.defaultVibrator
    } else {
        @Suppress("DEPRECATION")
        getSystemService(Vibrator::class.java)
    }

@Composable
fun rememberHaptics(): TurnHubHaptics {
    val context = LocalContext.current
    return remember(context) { TurnHubHaptics(context.applicationContext.vibrator()) }
}
