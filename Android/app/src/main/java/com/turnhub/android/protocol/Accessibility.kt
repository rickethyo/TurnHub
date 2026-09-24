package com.turnhub.android.protocol

/** How a player's Sigil lights behave (`ledStyle` on the wire). Presentation only. */
enum class LedStyle(val wire: String, val label: String, val description: String) {
    STANDARD("standard", "Standard", "Breathing and pulsing lights, as before."),
    REDUCED_MOTION(
        "reduced-motion",
        "Reduced motion",
        "Steady lights and slow blinks only; your turn is bright, waiting is dim. Also safe without colour.",
    ),
    MONOCHROME_SAFE("monochrome-safe", "Monochrome-safe", "Standard lights, but no two signals differ by colour alone."),
    ;

    companion object {
        fun fromWire(value: String): LedStyle? = entries.firstOrNull { it.wire == value }
    }
}

/** The hold-time choices Atlas accepts (all in milliseconds). */
data class HoldLimits(
    val longPressMinMs: Int,
    val longPressMaxMs: Int,
    val winHoldMinMs: Int,
    val winHoldMaxMs: Int,
    val minGapMs: Int,
    val stepMs: Int,
) {
    fun longPressChoices(): List<Int> = (longPressMinMs..longPressMaxMs step stepMs).toList()
    fun winHoldChoices(): List<Int> = (winHoldMinMs..winHoldMaxMs step stepMs).toList()
    fun allows(longPressMs: Int, winHoldMs: Int): Boolean =
        longPressMs in longPressMinMs..longPressMaxMs && winHoldMs in winHoldMinMs..winHoldMaxMs &&
            winHoldMs >= longPressMs + minGapMs
}

/**
 * The signed-in player's Sigil accessibility preferences
 * (`GET/POST /api/session/accessibility`, protocol/accessibility-v1.schema.json).
 * Atlas owns and stores them with the profile; the app only edits a copy.
 */
data class AccessibilitySettings(
    val sigilSound: Boolean,
    val ledStyle: LedStyle,
    val longPressMs: Int,
    val winHoldMs: Int,
    /** False when Atlas could not read the saved record and is showing defaults. */
    val stored: Boolean,
    val limits: HoldLimits,
)
