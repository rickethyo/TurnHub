package com.turnhub.android.domain

/**
 * The Emperor variant (CR 809, Comprehensive Rules of 2026-09-25): two teams of
 * three. Seats follow turn order (0-based, playerNumber - 1): seats 0-2 are team
 * A and 3-5 team B, each with its emperor in the middle (seats 1 and 4) and a
 * general either side, so the generals at seats 2 and 3, and 5 and 0, face each
 * other. Relationships follow seat indices only, never names or hardware.
 *
 * Range of influence (801, 809.3a) is 2 seats for emperors and 1 for generals,
 * counted over the players still in the game, so it closes up when a player
 * leaves (801.2c). A player can attack only an opponent seated next to them
 * (809.3c). A team wins when its emperor wins and loses when its emperor loses
 * (809.5); TurnHub ends the game when an emperor is out.
 */
object Emperor {
    const val PLAYERS = 6
    const val TEAM_SIZE = 3

    fun team(seat: Int): Int = seat / TEAM_SIZE

    fun isEmperor(seat: Int): Boolean = seat % TEAM_SIZE == 1

    fun emperorOf(team: Int): Int = team * TEAM_SIZE + 1

    fun teammates(a: Int, b: Int): Boolean = team(a) == team(b)

    fun teamSeats(team: Int): List<Int> = (team * TEAM_SIZE until (team + 1) * TEAM_SIZE).toList()

    /** "A" or "B". */
    fun teamName(team: Int): String = ('A' + team).toString()

    /** 809.3a: emperors reach 2 seats, generals 1. */
    fun range(seat: Int): Int = if (isEmperor(seat)) 2 else 1

    /** Seats between [a] and [b] counting only players still in, the shorter way round; null when either is out. */
    fun distance(a: Int, b: Int, eliminated: List<Boolean>): Int? {
        if (eliminated.size != PLAYERS || eliminated[a] || eliminated[b]) return null
        val living = eliminated.indices.filter { !eliminated[it] }
        val d = Math.abs(living.indexOf(a) - living.indexOf(b))
        return minOf(d, living.size - d)
    }

    /** The other players [seat] can target and affect (801.2), nearest first; empty when [seat] is out. */
    fun inRange(seat: Int, eliminated: List<Boolean>): List<Int> =
        eliminated.indices.filter { it != seat }
            .mapNotNull { other -> distance(seat, other, eliminated)?.takeIf { it <= range(seat) }?.let { other to it } }
            .sortedBy { it.second }.map { it.first }

    /** The opponents [seat] can attack: those seated right next to them (809.3c). */
    fun attackable(seat: Int, eliminated: List<Boolean>): List<Int> =
        eliminated.indices.filter { it != seat && !teammates(seat, it) && distance(seat, it, eliminated) == 1 }

    /**
     * Who wins once a batch of eliminations is applied: the whole team (fallen
     * generals too) whose rival emperor is out; empty (a draw) when both emperors
     * are out; null while both emperors are in. Needs exactly six seats.
     */
    fun winners(eliminated: List<Boolean>): List<Int>? {
        if (eliminated.size != PLAYERS) return null
        val out = (0..1).filter { eliminated[emperorOf(it)] }
        return when (out.size) {
            0 -> null
            1 -> teamSeats(1 - out.single())
            else -> emptyList()
        }
    }
}
