package com.turnhub.android.protocol

import com.turnhub.android.testing.Fixtures
import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertThrows
import org.junit.Assert.assertTrue
import org.junit.Test

class AtlasWireParserTest {

    @Test
    fun `parses the info fixture`() {
        val info = AtlasWireParser.parseInfo(Fixtures.text("info.response.json"))

        assertEquals("TurnHub", info.product)
        assertEquals("Atlas", info.deviceType)
        assertEquals("THA-025448410001", info.atlasId)
        assertEquals("0.6.0-dev", info.firmwareVersion)
        assertEquals("1", info.apiVersion)
        assertEquals("0.1", info.protocolVersion)
        assertEquals(1, info.radioProtocolVersion)
        assertEquals(Fixtures.BOOT_ID, info.bootId)
        assertEquals(0L, info.revision)
        assertEquals(
            AtlasCapabilities(
                stateSnapshot = true,
                sessionControls = true,
                intentEnvelope = false,
                events = false,
                requestDeduplication = false,
            ),
            info.capabilities,
        )
    }

    @Test
    fun `parses every state fixture`() {
        listOf("lobby", "running", "reconnected", "commander").forEach { name ->
            val state = AtlasWireParser.parseState(Fixtures.text("$name.response.json"))
            assertEquals(name, "THA-025448410001", state.atlasId)
            assertEquals(name, Fixtures.BOOT_ID, state.bootId)
        }
    }

    @Test
    fun `parses the running fixture faithfully`() {
        val state = Fixtures.state("running.response.json")

        assertEquals("0.1", state.protocolVersion)
        assertEquals(4L, state.revision)
        assertEquals(TableState.RUNNING, state.state)
        assertEquals(8, state.hostModuleId)
        assertEquals(1, state.starterPlayer)
        assertEquals(1, state.activePlayer)
        assertNull(state.winnerPlayer)
        assertEquals(TableSettings(GameProfile.GENERIC, 40), state.settings)
        assertEquals(4101L, state.sampledAtMs)
        assertEquals(100L, state.gameElapsedMs)
        assertEquals(100L, state.turnElapsedMs)
        assertEquals(PendingDecisions(null, 0, null, null, null), state.pending)
        assertEquals(2, state.players.size)
        assertEquals(
            Player(
                playerNumber = 1,
                moduleId = 8,
                slot = 1,
                participantId = 28,
                profileId = null,
                displayName = null,
                eliminated = false,
                life = 40,
                turnsCompleted = 0,
                commanderDamage = emptyList(),
                lifeRequest = null,
            ),
            state.players[0],
        )
    }

    @Test
    fun `parses commander damage and life requests`() {
        val state = Fixtures.state("commander.response.json")

        assertEquals(GameProfile.MTG_COMMANDER, state.settings.profile)
        assertEquals(listOf(CommanderDamage(sourcePlayer = 2, damage = listOf(0, 3))), state.players[0].commanderDamage)
        assertEquals(
            LifeRequest(id = 2, actor = 1, target = 2, delta = -2, state = LifeRequestState.PENDING, requestedAtMs = 4000),
            state.players[1].lifeRequest,
        )
    }

    @Test
    fun `lobby life is null`() {
        val lobbyPlayer = Fixtures.json("running.response.json").getJSONArray("players").getJSONObject(0)
            .put("life", JSONObject.NULL)
        val state = Fixtures.state("lobby.response.json") { getJSONArray("players").put(lobbyPlayer) }

        assertEquals(TableState.LOBBY, state.state)
        assertNull(state.players.single().life)
        assertNull(state.hostModuleId)
        assertNull(state.activePlayer)
    }

    @Test
    fun `unsigned 32-bit values keep their full range`() {
        val max = 4_294_967_295L
        val state = Fixtures.state("running.response.json") {
            put("revision", max)
            put("sampledAtMs", max)
            getJSONArray("players").getJSONObject(0).put("participantId", max).put("turnsCompleted", max)
        }

        assertEquals(max, state.revision)
        assertEquals(max, state.sampledAtMs)
        assertEquals(max, state.players[0].participantId)
        assertEquals(max, state.players[0].turnsCompleted)
    }

    @Test
    fun `revision outside unsigned 32-bit range is malformed`() {
        assertThrows(AtlasWireException.Malformed::class.java) {
            Fixtures.state("running.response.json") { put("revision", 4_294_967_296L) }
        }
        assertThrows(AtlasWireException.Malformed::class.java) {
            Fixtures.state("running.response.json") { put("revision", -1) }
        }
    }

    @Test
    fun `optional names are read when Atlas sends them`() {
        val state = Fixtures.state("running.response.json") {
            getJSONArray("players").getJSONObject(0).put("displayName", "Ricky").put("profileId", "A1B2C3D4")
        }

        assertEquals("Ricky", state.players[0].displayName)
        assertEquals("A1B2C3D4", state.players[0].profileId)
    }

    @Test
    fun `unknown fields are ignored for forward compatibility`() {
        val state = Fixtures.state("running.response.json") { put("futureField", JSONObject().put("x", 1)) }
        assertEquals(4L, state.revision)
    }

    @Test
    fun `missing required state fields are malformed`() {
        listOf("revision", "bootId", "settings", "players", "pending", "turnElapsedMs").forEach { key ->
            assertThrows(key, AtlasWireException.Malformed::class.java) {
                Fixtures.state("running.response.json") { remove(key) }
            }
        }
        assertThrows(AtlasWireException.Malformed::class.java) {
            Fixtures.state("running.response.json") { getJSONArray("players").getJSONObject(0).remove("life") }
        }
    }

    @Test
    fun `unknown enum values fail closed`() {
        assertThrows(AtlasWireException.Malformed::class.java) {
            Fixtures.state("running.response.json") { put("state", "OVERTIME") }
        }
        assertThrows(AtlasWireException.Malformed::class.java) {
            Fixtures.state("running.response.json") { getJSONObject("settings").put("profile", "chess") }
        }
    }

    @Test
    fun `wrong field types are malformed`() {
        assertThrows(AtlasWireException.Malformed::class.java) {
            Fixtures.state("running.response.json") { put("revision", "4") }
        }
        assertThrows(AtlasWireException.Malformed::class.java) {
            Fixtures.state("running.response.json") { put("revision", 4.5) }
        }
        assertThrows(AtlasWireException.Malformed::class.java) {
            Fixtures.state("running.response.json") { put("bootId", "not-a-boot-id") }
        }
    }

    @Test
    fun `non-TurnHub responders are rejected`() {
        val notTurnHub = listOf(
            "<html><body>Router login</body></html>",
            "",
            """{"status":"ok"}""",
            Fixtures.json("info.response.json").put("product", "Other").toString(),
            Fixtures.json("info.response.json").put("deviceType", "Sigil").toString(),
        )
        notTurnHub.forEach { body ->
            assertThrows(body, AtlasWireException.NotTurnHub::class.java) { AtlasWireParser.parseInfo(body) }
        }
    }

    @Test
    fun `TurnHub info with broken fields is malformed, not foreign`() {
        val error = assertThrows(AtlasWireException.Malformed::class.java) {
            Fixtures.info { remove("capabilities") }
        }
        assertTrue(error.message!!.contains("capabilities"))
    }
}
