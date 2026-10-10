package com.turnhub.android.protocol

/** Atlas-owned Magic counters. Tax is derived from casts from the command zone. */
data class PlayerCounters(
    val poison: Int = 0,
    val energy: Int = 0,
    val experience: Int = 0,
    val commanderCasts: List<Int> = listOf(0, 0),
    val cityBlessing: Boolean = false,
) {
    fun commanderTax(commander: Int): Int = commanderCasts[commander - 1] * 2
}
