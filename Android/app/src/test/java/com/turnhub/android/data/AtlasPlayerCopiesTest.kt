package com.turnhub.android.data

import com.turnhub.android.standalone.*
import com.turnhub.android.ui.home.hostedGameJson
import kotlinx.coroutines.test.runTest
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test

class AtlasPlayerCopiesTest {
    @Test fun `returned history binds same IDs on another phone without Atlas account links`() = runTest {
        var now = 0L
        val source = StandaloneTable(MemoryStandaloneStore(StandaloneGame(), LocalLibrary()), wallClock = { now }, shortGameMs = 0)
        source.addPlayer("Alex"); source.addPlayer("Alex")
        val players = source.state.value.library.players
        source.start(); now = 100_000; source.control(TabletSeat(9, 1), "win")
        val record = source.state.value.library.history.single().let { r ->
            r.copy(players = r.players.mapIndexed { index, seat -> seat.copy(profileId = players[index].profileId) })
        }
        val copied = players.mapIndexed { index, player -> player.copy(localId = "other-phone-$index") }
        val target = StandaloneTable(MemoryStandaloneStore(StandaloneGame(), LocalLibrary(players = copied)))
        assertEquals(1, target.addAtlasRecords("atlas", listOf(record.toJson())))
        assertEquals(copied.map { it.localId }, target.state.value.library.history.single().players.map { it.localId })
        assertEquals(0, target.addAtlasRecords("atlas", listOf(record.toJson())))
        assertEquals(com.turnhub.android.standalone.DeliveryStatus.IMPORTED,
            target.state.value.library.delivery(record.recordId).status)
    }

    @Test fun `paired copies upload records without account links or credentials and retry once`() = runTest {
        var now = 0L
        val table = StandaloneTable(MemoryStandaloneStore(StandaloneGame(), LocalLibrary()), wallClock = { now }, shortGameMs = 0)
        table.addPlayer("Alex"); table.addPlayer("Alex")
        val players = table.state.value.library.players
        val hosted = hostedGameJson(table.state.value.game, players).getJSONArray("players")
        assertEquals(players.map { it.profileId }, (0..1).map { hosted.getJSONObject(it).getString("profileId") })
        table.start(); now = 100_000
        table.control(TabletSeat(9, 1), "win")
        val record = table.state.value.library.history.single()
        var attempts = 0
        var online = false
        val sent = mutableListOf<Map<String, String>>()
        val sync = AtlasPlayerCopies { method, path, fields ->
            when (path) {
                "/api/player-copies" -> {
                    if (method == "GET") return@AtlasPlayerCopies RawResponse(200,JSONObject().put("copies",
                        org.json.JSONArray().apply { players.forEach { put(SharedProfileCopy.encode(it)) } }).toString())
                    val values = fields.toMap(); sent += values
                    assertEquals(setOf("profileId", "name", "color", "avatar", "copy"), values.keys)
                    RawResponse(200, """{"ok":true}""")
                }
                "/api/standalone/import" -> {
                    attempts++
                    if (!online) null else RawResponse(200, """{"ok":true,"duplicate":false,"credited":2,"unmatched":[]}""")
                }
                "/api/player-copies/stats" -> RawResponse(200, """{"lifetime":{"gamesPlayed":3,"gamesWon":1}}""")
                "/api/player-copies/history" -> RawResponse(200,
                    JSONObject().put("records", org.json.JSONArray().put(record.toJson())).put("more", false).toString())
                else -> error(path)
            }
        }
        val first = sync.sync("atlas", table)
        assertEquals(2, first.profiles.size)
        assertEquals(2, sent.map { it.getValue("profileId") }.distinct().size)
        assertEquals(DeliveryStatus.PENDING, table.state.value.library.delivery(record.recordId).status)
        assertEquals(listOf(record), table.state.value.library.history)
        online = true
        val next = sync.sync("atlas", table)
        assertEquals(DeliveryStatus.IMPORTED, table.state.value.library.delivery(record.recordId).status)
        assertEquals(players.map { it.profileId }, table.state.value.library.delivery(record.recordId).profileIds)
        assertEquals(3 to 1, next.atlasTotals[players.first().localId])
        sync.sync("atlas", table)
        assertEquals(2, attempts)
    }

    @Test fun `unacknowledged copy never becomes a credited identity`() = runTest {
        val table = StandaloneTable(MemoryStandaloneStore(StandaloneGame(), LocalLibrary()))
        table.addPlayer("Alex")
        val result = AtlasPlayerCopies { _, path, _ ->
            assertEquals("/api/player-copies", path)
            RawResponse(503, "{}")
        }.sync("atlas", table)
        assertTrue(result.profiles.isEmpty())
        assertNotNull(result.message)
    }
}
