package com.turnhub.android.standalone

import org.json.JSONArray

/**
 * One player's games travel with them (owner, 2026-11-05; plans/profile-sync.md): two devices
 * swap the finished games of one profile and keep the union, matched by [GameRecord.recordId].
 *
 * - Identity is the profile ID, never the name.
 * - A record already here is never replaced. A copy whose outcome differs is skipped and
 *   counted as a conflict, so a bad copy cannot overwrite a real game or count twice.
 * - Only the syncing profile's own seat binds to a local account; opponents keep their profile
 *   IDs, and their own sync binds their seats.
 */
object HistorySync {
    /** Records per page, as account copies use; keeps a frame well under the LAN limit. */
    const val PAGE = 32
    /** Record IDs a peer may list in one index; more stay for the next sync. */
    const val MAX_INDEX = 5_000

    data class Merge(val library: LocalLibrary, val added: Int, val bound: Int, val conflicts: Int)

    private fun owner(library: LocalLibrary, profileId: String) =
        library.players.firstOrNull { it.profileId == profileId }

    /** IDs of every retained game the account with [profileId] played on this device. */
    fun ids(library: LocalLibrary, profileId: String): List<String> {
        val player = owner(library, profileId) ?: return emptyList()
        return library.history.filter { r -> r.players.any { it.localId == player.localId } }.map { it.recordId }
    }

    /**
     * A record as it leaves this device: no local account IDs; the owner's seat, and with [library]
     * every other account's seat, named by its public profile ID so each can bind on its own sync.
     */
    fun portable(record: GameRecord, player: SavedLocalPlayer, library: LocalLibrary? = null): GameRecord =
        record.copy(players = record.players.map { seat ->
            val account = if (seat.localId == player.localId) player
                else seat.localId?.let { id -> library?.players?.firstOrNull { it.localId == id } }
            seat.copy(localId = null, profileId = account?.profileId ?: seat.profileId)
        })

    /** The asked-for games of [profileId] in portable form; IDs it didn't play here are ignored. */
    fun export(library: LocalLibrary, profileId: String, ids: Collection<String>): JSONArray {
        val player = owner(library, profileId) ?: return JSONArray()
        val wanted = ids.toSet()
        return JSONArray().apply {
            library.history.filter { r -> r.recordId in wanted && r.players.any { it.localId == player.localId } }
                .take(PAGE).forEach { put(portable(it, player, library).toJson()) }
        }
    }

    private fun outcome(record: GameRecord) =
        record.copy(players = record.players.map { it.copy(localId = null, profileId = null) }).toJson().toString()

    /**
     * Adds [records] (portable form) to [profileId]'s local account. Each must name that profile
     * on exactly one seat. Throws only when the account is not on this device.
     */
    fun merge(library: LocalLibrary, profileId: String, records: JSONArray): Merge {
        val player = requireNotNull(owner(library, profileId)) { "No account with that profile here" }
        var next = library
        var added = 0; var bound = 0; var conflicts = 0
        for (i in 0 until minOf(records.length(), PAGE)) {
            val incoming = records.optJSONObject(i)?.let(GameRecord::fromJson)
            val seat = incoming?.players?.indexOfFirst { it.profileId == profileId } ?: -1
            if (incoming == null || seat < 0 || incoming.players.count { it.profileId == profileId } != 1) { conflicts++; continue }
            val bound0 = incoming.copy(players = incoming.players.mapIndexed { index, p ->
                p.copy(localId = if (index == seat) player.localId else null) })
            val old = next.history.firstOrNull { it.recordId == incoming.recordId }
            when {
                old == null -> { next = next.withRecord(bound0); added++ }
                outcome(old) != outcome(bound0) -> conflicts++
                old.players[seat].localId == player.localId -> Unit
                old.players.any { it.localId == player.localId } || old.players[seat].localId != null -> conflicts++
                else -> {
                    next = next.copy(history = next.history.map { r -> if (r.recordId != old.recordId) r else
                        r.copy(players = r.players.mapIndexed { index, p -> if (index == seat) p.copy(localId = player.localId) else p }) })
                    bound++
                }
            }
        }
        return Merge(next, added, bound, conflicts)
    }
}
