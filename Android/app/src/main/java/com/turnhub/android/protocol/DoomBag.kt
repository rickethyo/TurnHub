package com.turnhub.android.protocol

/** Match-scoped bag projection. Numbers are 1-based; 0 means no player. */
data class DoomBag(
    val atRisk: Int = 0,
    val drawn: Int = 0,
    val boundary: Int = 0,
    val warmup: Int = 0,
    val cycle: Long = 0,
    val pending: Boolean = false,
    val pendingTurn: Int = 0,
    /** Internal saved selection; connected projections send only [sealed]. */
    val selected: Int = 0,
    val sealed: Boolean = false,
    val lastDraw: Int = 0,
    val lastCheck: Long = 0,
) {
    fun contains(player: Int): Boolean = player in 1..16 && atRisk and (1 shl (player - 1)) != 0
}

/** Rejects an action from a different match, boot, cycle or stale screen. */
data class DoomToken(val bootId: String, val revision: Long, val cycle: Long, val game: Int = 1)
enum class DoomAction(val wire: String) { DRAW("draw"), ELIGIBILITY("eligibility"), CREDIT("credit") }
