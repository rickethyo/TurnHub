package com.turnhub.android.protocol

/**
 * Mirrors `planechase` in protocol/state-v0.1.schema.json: Planechase on Atlas
 * with the players' own plane cards. Atlas counts; the app only shows it.
 */
data class AtlasPlanechase(
    val planeswalks: Int,
    val chaos: Int,
    /** Counters on the face-up plane. */
    val counters: Int,
    /** Mana the active player's next planar die roll costs this turn. */
    val rollCost: Int,
    val lastRoll: Roll?,
) {
    data class Roll(val number: Int, val player: Int, val face: String, val cost: Int, val fromEffect: Boolean)
}
