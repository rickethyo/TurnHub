package com.turnhub.android.standalone

import com.turnhub.android.domain.TableClock

/** One saved player's line on the open standalone stats board. */
data class LeaderboardRow(val player: SavedLocalPlayer, val stats: LocalStats) {
    val winPercent: Int get() = if (stats.totals.played > 0) stats.totals.won * 100 / stats.totals.played else 0
}

/**
 * Every saved player who has finished a game on this device, most wins first
 * (then win rate, then fewest games). Stats are open to everyone at the table:
 * no account PIN guards them.
 */
fun LocalLibrary.leaderboard(): List<LeaderboardRow> = players
    .map { LeaderboardRow(it, stats(it.localId)) }
    .filter { it.stats.totals.played > 0 }
    .sortedWith(compareByDescending<LeaderboardRow> { it.stats.totals.won }
        .thenByDescending { it.winPercent }
        .thenBy { it.stats.totals.played }
        .thenBy { it.player.name.lowercase() })

/** A player's statistics as a message for the Android share sheet. */
fun LocalLibrary.statsShareText(localId: String): String {
    val player = requireNotNull(players.firstOrNull { it.localId == localId })
    val stats = stats(localId)
    val t = stats.totals
    val f = TableClock::format
    val rate = if (t.played > 0) t.won * 100 / t.played else 0
    return buildString {
        appendLine("${player.name}'s TurnHub stats")
        appendLine("${t.played} games · ${t.won} wins ($rate%)${if (t.draws > 0) " · ${t.draws} draws" else ""}")
        appendLine("Went first ${stats.started} · knocked out ${stats.eliminated} · conceded ${stats.conceded}")
        appendLine("${stats.turnsCompleted} turns · average ${f(stats.averageTurnMs)} · fastest ${f(stats.fastestTurnMs)} · longest ${f(stats.longestTurnMs)}")
        appendLine("Average game ${f(stats.averageGameMs)}")
        if (stats.starPlayed > 0) appendLine("Star Commander: ${stats.starPlayed} played · ${stats.starWon} won")
        append("Tracked with TurnHub")
    }
}

/**
 * A finished game as a message for the share sheet: who won, the format and
 * length, then every player in turn order. [formatName] is the format's
 * display name (Commander, Yu-Gi-Oh!, ...).
 */
fun GameRecord.shareSummary(formatName: String): String {
    // A Two-Headed Giant winner's teammate won too.
    val names = players.indices.filter { won(it) }.map { players[it].name }
    val headline = when {
        names.isEmpty() -> "Game over: a draw"
        names.size == 1 -> "🏆 ${names.single()} won"
        twoHeadedGiant && winners.size == 1 -> "🏆 ${names.joinToString(" and ")} won"
        else -> "🏆 ${names.joinToString(" and ")} shared the win"
    }
    return buildString {
        appendLine("$headline!")
        appendLine("$formatName${if (twoHeadedGiant) " · Two-Headed Giant" else ""} · ${players.size} players · ${TableClock.format(durationMs)}")
        players.forEachIndexed { i, p ->
            val place = when {
                won(i) -> "winner"
                // The first player out finishes last.
                p.outOrder != null -> "${ordinal(players.size - p.outOrder + 1)}${if (p.conceded) " (conceded)" else ""}"
                else -> null
            }
            append("${p.name}: ${p.finalLife} life · ${p.turnsCompleted} turns")
            if (place != null) append(" · $place")
            appendLine()
        }
        append("Played with TurnHub")
    }
}

/** 1st, 2nd, 3rd, 4th ... 11th, 12th, 13th, 21st. */
internal fun ordinal(n: Int): String = "$n" + when {
    n % 100 in 11..13 -> "th"
    n % 10 == 1 -> "st"
    n % 10 == 2 -> "nd"
    n % 10 == 3 -> "rd"
    else -> "th"
}
