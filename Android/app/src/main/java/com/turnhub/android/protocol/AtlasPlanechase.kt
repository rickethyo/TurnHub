package com.turnhub.android.protocol

/**
 * Mirrors `planechase` in protocol/state-v0.1.schema.json: Planechase on Atlas
 * with the players' own plane cards. Atlas counts; the app only shows it.
 */
data class AtlasPlanechase(
    /** Atlas's digital deck; false for the players' own planes. */
    val digital: Boolean = false,
    /** Digital: the face-up card ([Plane.name] is null if Atlas's catalog lost it). */
    val plane: Plane? = null,
    val deckSize: Int = 0,
    val planeswalks: Int,
    val chaos: Int,
    /** Counters on the face-up plane. */
    val counters: Int,
    /** Mana the active player's next planar die roll costs this turn. */
    val rollCost: Int,
    val lastRoll: Roll?,
) {
    data class Plane(val id: String, val name: String?, val phenomenon: Boolean)
    data class Roll(val number: Int, val player: Int, val face: String, val cost: Int, val fromEffect: Boolean)
}
