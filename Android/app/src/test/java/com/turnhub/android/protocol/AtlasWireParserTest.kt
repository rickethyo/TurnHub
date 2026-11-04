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
        assertEquals("1", info.protocolVersion)
        assertEquals(3, info.radioProtocolVersion)
        assertEquals(Fixtures.BOOT_ID, info.bootId)
        assertEquals(0L, info.revision)
        assertEquals(
            AtlasCapabilities(
                stateSnapshot = true,
                sessionControls = false,
                intentEnvelope = false,
                events = false,
                requestDeduplication = false,
            ),
            info.capabilities,
        )
    }

    @Test
    fun `parses an idle Atlas with no hub`() {
        val state = Fixtures.state("state-idle.response.json")

        assertEquals("1", state.protocolVersion)
        assertEquals("THA-025448410001", state.atlasId)
        assertEquals(Fixtures.BOOT_ID, state.bootId)
        assertEquals(1L, state.revision)
        assertNull(state.hub)
        assertNull(state.battery)
        assertEquals(AtlasLinks(hotspot = true, home = false, sigils = 0), state.links)
    }

    @Test
    fun `parses the hub holding the table, its battery and links`() {
        val state = Fixtures.state("state-hosted.response.json")

        assertEquals(AtlasHub("Kitchen tablet", "m-20261104-1", TableState.RUNNING, players = 4), state.hub)
        assertEquals(AtlasBattery(percent = 64, low = false, charging = true), state.battery)
        assertEquals(AtlasLinks(hotspot = true, home = true, sigils = 2), state.links)
    }

    @Test
    fun `hub and battery are required but may be null, links are required`() {
        listOf("hub", "battery", "links", "revision", "bootId", "atlasId", "protocolVersion").forEach { key ->
            assertThrows(key, AtlasWireException.Malformed::class.java) {
                Fixtures.state("state-hosted.response.json") { remove(key) }
            }
        }
        assertThrows(AtlasWireException.Malformed::class.java) {
            Fixtures.state("state-hosted.response.json") { put("links", JSONObject.NULL) }
        }
    }

    @Test
    fun `bad hub battery and link values are malformed`() {
        listOf<JSONObject.() -> Unit>(
            { getJSONObject("hub").put("state", "OVERTIME") },
            { getJSONObject("hub").put("players", -1) },
            { getJSONObject("hub").remove("gameId") },
            { getJSONObject("battery").put("percent", 101) },
            { getJSONObject("battery").remove("charging") },
            { getJSONObject("links").put("sigils", 99) },
            { put("revision", 4_294_967_296L) },
            { put("revision", "4") },
            { put("bootId", "not-a-boot-id") },
        ).forEach { edit ->
            assertThrows(AtlasWireException.Malformed::class.java) {
                Fixtures.state("state-hosted.response.json", edit)
            }
        }
    }

    @Test
    fun `unknown fields are ignored for forward compatibility`() {
        val state = Fixtures.state("state-idle.response.json") { put("futureField", JSONObject().put("x", 1)) }
        assertEquals(1L, state.revision)
        assertEquals(4_294_967_295L, Fixtures.state("state-idle.response.json") { put("revision", 4_294_967_295L) }.revision)
    }

    @Test
    fun `parses the preset icons and skips a malformed one`() {
        val row = "#".repeat(8) + ".".repeat(8)
        val rows = List(16) { "\"$row\"" }.joinToString(",")
        val icons = AtlasWireParser.parseAvatars(
            """{"size":16,"avatars":[{"id":1,"key":"die","label":"Die","rows":[$rows]},
               {"id":2,"key":"bad","label":"Bad","rows":["#"]}]}""",
        )
        assertEquals(1, icons.size)
        assertEquals("Die", icons[0].label)
        assertTrue(icons[0].ink(0, 0))
        assertTrue(!icons[0].ink(8, 0))
    }

    @Test
    fun `parses the shared accessibility fixture`() {
        val settings = AtlasWireParser.parseAccessibility(Fixtures.text("accessibility.response.json"))

        assertEquals(false, settings.sigilSound)
        assertEquals(LedStyle.REDUCED_MOTION, settings.ledStyle)
        assertEquals(3000 to 6000, settings.longPressMs to settings.winHoldMs)
        assertTrue(settings.stored)
        assertEquals((1000..4000 step 250).toList(), settings.limits.longPressChoices())
        assertTrue(settings.limits.allows(2000, 5000))
        assertEquals(false, settings.limits.allows(4000, 4500))
        assertEquals(30000, settings.lifeApprovalMs)
        assertEquals(listOf(15000, 30000, 60000), settings.lifeApprovalOptionsMs)
    }

    @Test
    fun `rejects unknown light styles and impossible hold times`() {
        val fixture = Fixtures.json("accessibility.response.json")
        for (edit in listOf<(org.json.JSONObject) -> Unit>(
            { it.put("ledStyle", "sparkly") },
            { it.put("winHoldMs", 3500) },
            { it.remove("limits") },
            { it.put("lifeApprovalMs", 20000) },
        )) {
            val body = org.json.JSONObject(fixture.toString()).also(edit).toString()
            assertThrows(AtlasWireException.Malformed::class.java) { AtlasWireParser.parseAccessibility(body) }
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
