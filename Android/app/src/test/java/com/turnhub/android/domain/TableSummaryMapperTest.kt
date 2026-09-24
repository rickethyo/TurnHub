package com.turnhub.android.domain

import com.turnhub.android.protocol.GameProfile
import com.turnhub.android.protocol.TableState
import com.turnhub.android.testing.Fixtures
import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertThrows
import org.junit.Assert.assertTrue
import org.junit.Test

class TableSummaryMapperTest {

    @Test
    fun `maps a running snapshot into a table summary`() {
        val summary = TableSummaryMapper.map(Fixtures.info(), Fixtures.state("reconnected.response.json"))

        assertEquals("THA-025448410001", summary.atlasId)
        assertEquals(Fixtures.BOOT_ID, summary.bootId)
        assertEquals("0.6.0-dev", summary.firmwareVersion)
        assertEquals(6L, summary.revision)
        assertEquals(TableState.RUNNING, summary.state)
        assertEquals(GameProfile.GENERIC, summary.settings.profile)
        assertEquals(40, summary.settings.startingLife)
        assertEquals(ControllerHandle(8), summary.host)
        assertEquals(1, summary.starterPlayerNumber)
        assertEquals(2, summary.activePlayerNumber)
        assertNull(summary.winnerPlayerNumber)
        assertEquals(3000L, summary.turnElapsedMs)
        assertEquals(listOf(1, 2), summary.players.map { it.playerNumber })
        assertEquals(listOf(40, 40), summary.players.map { it.life })
        assertEquals(listOf(1L, 0L), summary.players.map { it.turnsCompleted })
        assertEquals(listOf(28L, 29L), summary.players.map { it.participantId })
    }

    @Test
    fun `unnamed players get neutral labels`() {
        val summary = TableSummaryMapper.map(Fixtures.info(), Fixtures.state("running.response.json"))
        assertEquals(listOf("Player 1", "Player 2"), summary.players.map { it.label })
    }

    @Test
    fun `Atlas-provided names are used and blank names fall back`() {
        val state = Fixtures.state("running.response.json") {
            val players = getJSONArray("players")
            players.getJSONObject(0).put("displayName", "Ricky")
            players.getJSONObject(1).put("displayName", "  ")
        }
        val summary = TableSummaryMapper.map(Fixtures.info(), state)
        assertEquals(listOf("Ricky", "Player 2"), summary.players.map { it.label })
    }

    @Test
    fun `controller handles split physical and virtual`() {
        assertEquals(ControllerHandle.Kind.PHYSICAL, ControllerHandle(0).kind)
        assertEquals(ControllerHandle.Kind.PHYSICAL, ControllerHandle(7).kind)
        assertEquals(ControllerHandle.Kind.VIRTUAL, ControllerHandle(8).kind)
        assertEquals(ControllerHandle.Kind.VIRTUAL, ControllerHandle(23).kind)
        assertEquals(ControllerHandle.Kind.UNKNOWN, ControllerHandle(24).kind)
    }

    @Test
    fun `virtual-only table lists no physical Sigils`() {
        val summary = TableSummaryMapper.map(Fixtures.info(), Fixtures.state("running.response.json"))
        assertTrue(summary.players.all { it.controller.kind == ControllerHandle.Kind.VIRTUAL })
        assertTrue(summary.physicalSigils.isEmpty())
    }

    @Test
    fun `physical Sigils are only those seated, grouped by handle with their seats`() {
        // Sigil 3 holds shared seats A and B; one player is on a phone (handle 10).
        val state = Fixtures.state("running.response.json") {
            val players = getJSONArray("players")
            val template = players.getJSONObject(0)
            val seats = listOf(Triple(1, 3, 1), Triple(2, 10, 1), Triple(3, 3, 2))
            put("players", org.json.JSONArray(seats.map { (number, module, slot) ->
                JSONObject(template.toString())
                    .put("playerNumber", number).put("moduleId", module).put("slot", slot)
            }))
        }
        val summary = TableSummaryMapper.map(Fixtures.info(), state)

        assertEquals(1, summary.physicalSigils.size)
        val sigil = summary.physicalSigils.single()
        assertEquals(ControllerHandle(3), sigil.controller)
        assertEquals(
            listOf(
                PhysicalSigilAtTable.Seat(slot = 1, playerNumber = 1, label = "Player 1"),
                PhysicalSigilAtTable.Seat(slot = 2, playerNumber = 3, label = "Player 3"),
            ),
            sigil.seats,
        )
    }

    @Test
    fun `commander fixture keeps damage and pending requests`() {
        val summary = TableSummaryMapper.map(Fixtures.info(), Fixtures.state("commander.response.json"))

        assertEquals(listOf(0, 1), summary.physicalSigils.map { it.controller.id })
        assertEquals(37, summary.players[0].life)
        assertEquals(listOf(0, 3), summary.players[0].commanderDamage.single().damage)
        assertEquals(-2, summary.players[1].lifeRequest?.delta)
    }

    @Test
    fun `lobby players have no life`() {
        val player = Fixtures.json("running.response.json").getJSONArray("players").getJSONObject(0)
            .put("life", JSONObject.NULL)
        val state = Fixtures.state("lobby.response.json") { getJSONArray("players").put(player) }
        val summary = TableSummaryMapper.map(Fixtures.info(), state)

        assertEquals(TableState.LOBBY, summary.state)
        assertNull(summary.players.single().life)
        assertNull(summary.host)
    }

    @Test
    fun `refuses to combine info and state from different boots`() {
        val otherBoot = Fixtures.state("running.response.json") { put("bootId", Fixtures.OTHER_BOOT_ID) }
        assertThrows(IllegalArgumentException::class.java) { TableSummaryMapper.map(Fixtures.info(), otherBoot) }
    }
}
