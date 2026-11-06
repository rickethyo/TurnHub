package com.turnhub.android.data

import com.turnhub.android.standalone.LocalImportState
import com.turnhub.android.standalone.LocalPersonalization
import com.turnhub.android.standalone.StandaloneTable
import com.turnhub.android.protocol.ProfileSummary
import org.json.JSONObject

/** Exchanges profile copies and records with a paired phone's credential.
 * PIN verifiers remain protected by pairing; plaintext PINs and device roles never travel.
 */
class AtlasPlayerCopies(private val raw: suspend (String, String, List<Pair<String, String>>) -> RawResponse?) {
    suspend fun pullDirectory(table: StandaloneTable, atlasId: String): List<ProfileSummary>? {
        val response = raw("GET", "/api/player-copies", emptyList()) ?: return null
        if (!response.ok) return null
        return runCatching {
            val array = JSONObject(response.body).getJSONArray("copies")
            require(array.length() <= 160)
            val copies = (0 until array.length()).map { array.getJSONObject(it) }
            require(table.receiveSharedProfiles(atlasId, copies))
            copies.map { ProfileSummary(it.getString("profileId"),it.getString("name"),!it.isNull("credential")) }
        }.getOrNull()
    }

    suspend fun sync(atlasId: String, table: StandaloneTable): LocalImportState {
        val copied = mutableListOf<ProfileSummary>()
        val players = table.state.value.library.players
        var failed = false
        for (player in players) {
            val look = player.personalization
            val response = raw("POST", "/api/player-copies", listOf(
                "profileId" to player.profileId, "name" to player.name,
                "color" to (look.color ?: "none"),
                "avatar" to LocalPersonalization.AVATARS.indexOf(look.avatar).coerceAtLeast(0).toString(),
                "copy" to com.turnhub.android.standalone.SharedProfileCopy.encode(player).toString(),
            ))
            val answer = response?.takeIf { it.ok }?.let { runCatching { JSONObject(it.body) }.getOrNull() }
            if (answer?.optBoolean("ok") == true) {
                copied += ProfileSummary(player.profileId, player.name, false)
                if (!answer.optBoolean("appearanceStored", true)) failed = true
            } else failed = true
        }
        val directory = pullDirectory(table, atlasId)
        if (directory == null) failed = true
        table.queueAutomaticImports(atlasId, copied.map { it.profileId }.toSet(), guests = true, copies = true)
        table.sendRecords(atlasId) { raw("POST", "/api/standalone/import", it) }
        if (table.state.value.library.deliveries.any {
            it.atlasId == atlasId && it.status in setOf(
                com.turnhub.android.standalone.DeliveryStatus.PENDING,
                com.turnhub.android.standalone.DeliveryStatus.REJECTED,
            )
        }) failed = true
        val totals = mutableMapOf<String, Pair<Int, Int>>()
        for (player in players.filter { p -> copied.any { it.profileId == p.profileId } }) {
            val fields = listOf("profileId" to player.profileId)
            val response = raw("GET", "/api/player-copies/stats", fields)
            if (response?.ok == true) runCatching {
                JSONObject(response.body).getJSONObject("lifetime").let {
                    totals[player.localId] = it.getInt("gamesPlayed") to it.getInt("gamesWon")
                }
            }.onFailure { failed = true } else failed = true
            // No SD card means no archived history. NVS totals above still work.
            var skip = 0
            while (true) {
                val page = raw("GET", "/api/player-copies/history", fields + listOf("skip" to "$skip", "limit" to "20"))
                if (page?.ok != true) break
                val json = runCatching { JSONObject(page.body) }.getOrNull() ?: break
                val records = json.optJSONArray("records") ?: break
                val batch = (0 until records.length()).mapNotNull { records.optJSONObject(it) }
                table.addAtlasRecords(atlasId, batch)
                if (!json.optBoolean("more") || batch.isEmpty()) break
                skip += batch.size
                if (skip >= 2000) break
            }
        }
        return LocalImportState(atlasId, directory ?: copied, atlasTotals = totals, paired = true,
            message = if (failed) "Some player copies, records or statistics could not be saved or read. Check each game’s Details and Atlas storage, then sync again."
                else "Atlas sync complete. Your games remain saved on this device.")
    }
}
