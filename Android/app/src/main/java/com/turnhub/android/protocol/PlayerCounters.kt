package com.turnhub.android.protocol

/**
 * Magic counters. Tax is derived from casts from the command zone. [treasure]
 * and [rad] are kept only by phone-hosted games until Atlas tracks them.
 */
data class PlayerCounters(
    val poison: Int = 0,
    val energy: Int = 0,
    val experience: Int = 0,
    val commanderCasts: List<Int> = listOf(0, 0),
    val cityBlessing: Boolean = false,
    val treasure: Int = 0,
    val rad: Int = 0,
) {
    fun commanderTax(commander: Int): Int = commanderCasts[commander - 1] * 2
}
