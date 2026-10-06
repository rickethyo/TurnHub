package com.turnhub.android.data

import org.json.JSONException
import org.json.JSONObject

/**
 * The signed-in profile's statistics from `GET /api/session/stats`, as the
 * portal's Statistics page shows them. Times are milliseconds; Atlas computes
 * the averages. Without a microSD card ([detailed] false) only games played and
 * won, the last result and the last game type are recorded.
 */
data class ProfileStatistics(
    val name: String,
    val profileId: String,
    val winRate: String,
    val detailed: Boolean,
    val lifetime: Lifetime,
    val lastGame: LastGame,
    val moderation: Moderation,
) {
    data class Lifetime(
        val gamesPlayed: Long,
        val gamesWon: Long,
        val gamesStarted: Long,
        val gamesEliminated: Long,
        val turnsCompleted: Long,
        val totalTurnMs: Long,
        val averageTurnMs: Long,
        val fastestTurnMs: Long,
        val longestTurnMs: Long,
        val totalGameMs: Long,
        val averageGameMs: Long,
    )

    data class LastGame(
        val result: String,
        val durationMs: Long,
        val turns: Long,
        val averageTurnMs: Long,
        val fastestTurnMs: Long,
        val longestTurnMs: Long,
    )

    /** Private: counts only for a PIN-verified owner, otherwise the reason they are hidden. */
    data class Moderation(val visible: Boolean, val connectionResets: Long, val gameRemovals: Long, val reason: String?)

    /** Games won over games played, 0 to 1, for the win-rate ring. */
    val winFraction: Float
        get() = if (lifetime.gamesPlayed > 0) (lifetime.gamesWon.toFloat() / lifetime.gamesPlayed).coerceIn(0f, 1f) else 0f

    companion object {
        /** Lenient like the other session reads: missing numbers read as 0. Null when the body isn't JSON. */
        fun parse(body: String): ProfileStatistics? = try {
            val root = JSONObject(body)
            val l = root.optJSONObject("lifetime") ?: JSONObject()
            val g = root.optJSONObject("lastGame") ?: JSONObject()
            val m = root.optJSONObject("moderation") ?: JSONObject()
            ProfileStatistics(
                name = root.optString("name").ifBlank { "Unnamed profile" },
                profileId = root.optString("profileId"),
                winRate = root.optString("winRate").ifBlank { "0.0%" },
                detailed = root.optBoolean("detailed", true),
                lifetime = Lifetime(
                    gamesPlayed = l.optLong("gamesPlayed"),
                    gamesWon = l.optLong("gamesWon"),
                    gamesStarted = l.optLong("gamesStarted"),
                    gamesEliminated = l.optLong("gamesEliminated"),
                    turnsCompleted = l.optLong("turnsCompleted"),
                    totalTurnMs = l.optLong("totalTurnMs"),
                    averageTurnMs = l.optLong("averageTurnMs"),
                    fastestTurnMs = l.optLong("fastestTurnMs"),
                    longestTurnMs = l.optLong("longestTurnMs"),
                    totalGameMs = l.optLong("totalGameMs"),
                    averageGameMs = l.optLong("averageGameMs"),
                ),
                lastGame = LastGame(
                    result = g.optString("result").ifBlank { "None" },
                    durationMs = g.optLong("durationMs"),
                    turns = g.optLong("turns"),
                    averageTurnMs = g.optLong("averageTurnMs"),
                    fastestTurnMs = g.optLong("fastestTurnMs"),
                    longestTurnMs = g.optLong("longestTurnMs"),
                ),
                moderation = Moderation(
                    visible = m.optBoolean("visible"),
                    connectionResets = m.optLong("connectionResets"),
                    gameRemovals = m.optLong("gameRemovals"),
                    reason = m.optString("reason").ifBlank { null },
                ),
            )
        } catch (_: JSONException) {
            null
        }

        /** The portal's duration text: "1h 2m 3s" or "2m 3s"; "—" when there is nothing recorded. */
        fun duration(ms: Long, hasRecord: Boolean = true): String {
            if (!hasRecord) return "—"
            val total = ms.coerceAtLeast(0) / 1000
            val h = total / 3600
            val m = (total % 3600) / 60
            val s = total % 60
            return if (h > 0) "${h}h ${m}m ${s}s" else "${m}m ${s}s"
        }
    }
}

/** The last statistics read: the numbers, or why Atlas couldn't give them. */
data class StatisticsLoad(val stats: ProfileStatistics?, val error: String?)
