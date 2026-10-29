package com.turnhub.android.standalone

import org.json.JSONObject

/**
 * Match records as CSV, one row per player per game: the same text as the
 * shared core's `recordCsvRows` (core/src/turnhub_core.cpp), so the app and
 * Atlas export identical files. Both are checked against
 * protocol/examples/match-record.csv.
 */
object MatchCsv {
    const val HEADER = "record_id,started_at_utc,game_profile,two_headed_giant,starting_life,duration_ms,players,seat,name,profile_id," +
        "starter,won,draw,out_order,conceded,eliminated_by_seat,self_eliminated,final_life,turns,turn_ms," +
        "average_turn_ms,fastest_turn_ms,longest_turn_ms,commander_damage_received,poison,energy,experience," +
        "commander_casts_1,commander_casts_2,city_blessing,demerits,extensions_used,bag_drawn\n"

    /** Every record, oldest first, under one header. */
    fun of(records: List<GameRecord>): String = buildString {
        append(HEADER)
        records.sortedBy { it.startedAtMs }.forEach { append(rows(it.toJson())) }
    }

    fun rows(r: JSONObject): String = buildString {
        val players = r.getJSONArray("players")
        val winners = r.optJSONArray("winners")
        val winnerSet = (0 until (winners?.length() ?: 0)).map { winners!!.getInt(it) }.toSet()
        val common = listOf(field(r.getString("recordId")), isoUtc(r.getLong("startedAtMs")), field(r.getString("gameProfile")),
            r.optBoolean("twoHeadedGiant").toString(), number(r, "startingLife"), number(r, "durationMs"), "${players.length()}")
            .joinToString(",")
        val starter = if (r.isNull("starter")) -1 else r.getInt("starter")
        for (i in 0 until players.length()) {
            val p = players.getJSONObject(i)
            val c = p.getJSONObject("counters")
            val casts = c.getJSONArray("commanderCasts")
            val turns = p.getLong("turnsCompleted")
            val cells = listOf(
                common, "${i + 1}", field(p.getString("name")), field(if (p.isNull("profileId")) "" else p.getString("profileId")),
                (starter == i).toString(), (i in winnerSet).toString(), winnerSet.isEmpty().toString(),
                number(p, "outOrder"), p.optBoolean("conceded").toString(),
                if (p.isNull("eliminatedBy")) "" else "${p.getInt("eliminatedBy") + 1}",
                p.optBoolean("selfEliminated").toString(), number(p, "finalLife"), "$turns", number(p, "turnMs"),
                if (turns > 0) "${p.getLong("turnMs") / turns}" else "", number(p, "fastestTurnMs"), number(p, "longestTurnMs"),
                number(p, "commanderDamageReceived"), number(c, "poison"), number(c, "energy"), number(c, "experience"),
                "${casts.getLong(0)}", "${casts.getLong(1)}", c.optBoolean("cityBlessing").toString(),
                number(p, "demerits"), number(p, "extensionsUsed"), p.optBoolean("bagDrawn").toString(),
            )
            append(cells.joinToString(",")).append('\n')
        }
    }

    /** RFC 4180 quoting, and a leading ' on text a spreadsheet would run as a formula. */
    private fun field(text: String): String {
        val v = if (text.isNotEmpty() && text[0] in "=+-@\t\r") "'$text" else text
        return if (v.none { it == ',' || it == '"' || it == '\r' || it == '\n' }) v else "\"" + v.replace("\"", "\"\"") + "\""
    }

    private fun number(o: JSONObject, key: String): String = if (!o.has(key) || o.isNull(key)) "" else "${o.getLong(key)}"

    private fun isoUtc(ms: Long): String =
        java.time.Instant.ofEpochMilli(Math.floorDiv(ms, 1000L) * 1000L).truncatedTo(java.time.temporal.ChronoUnit.SECONDS)
            .let { java.time.format.DateTimeFormatter.ofPattern("yyyy-MM-dd'T'HH:mm:ss'Z'").withZone(java.time.ZoneOffset.UTC).format(it) }
}
