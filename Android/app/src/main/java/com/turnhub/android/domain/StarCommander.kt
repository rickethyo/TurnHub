package com.turnhub.android.domain

/**
 * Star Commander's relationships (Documentation/engineering/GAMEPLAY.md): five
 * seats in turn order, each seat's neighbours are its allies and the two seats
 * across the star are its enemies. Relationships follow seat indices only (0-based,
 * playerNumber - 1), never names, profiles or hardware; eliminated players keep
 * their seat. Atlas is the authority for Atlas games; the standalone game and the
 * screens use these same rules.
 */
object StarCommander {
    const val PLAYERS = 5

    /** The seats beside [seat]: the pentagon's edges. */
    fun allies(seat: Int): List<Int> = listOf((seat + 1) % PLAYERS, (seat + PLAYERS - 1) % PLAYERS)

    /** The seats across the star from [seat]: the pentagram's diagonals. */
    fun enemies(seat: Int): List<Int> = listOf((seat + 2) % PLAYERS, (seat + 3) % PLAYERS)

    fun allied(a: Int, b: Int): Boolean = a != b && b in allies(a)

    /**
     * Who wins once a batch of eliminations is applied: every living seat whose
     * two enemies are both out. Empty when nobody qualifies. Already eliminated
     * players never win. Needs exactly five seats.
     */
    fun winners(eliminated: List<Boolean>): List<Int> {
        if (eliminated.size != PLAYERS) return emptyList()
        return eliminated.indices.filter { seat -> !eliminated[seat] && enemies(seat).all { eliminated[it] } }
    }

    /** How many of [seat]'s two enemies are out. */
    fun enemiesOut(seat: Int, eliminated: List<Boolean>): Int =
        if (eliminated.size != PLAYERS) 0 else enemies(seat).count { eliminated[it] }
}
