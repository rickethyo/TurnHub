package com.turnhub.android.data

import com.turnhub.android.testing.Fixtures
import org.junit.Assert.assertEquals
import org.junit.Assert.assertThrows
import org.junit.Assert.assertTrue
import org.junit.Test

class AtlasEndpointAndCompatibilityTest {

    @Test
    fun `default endpoint is the Atlas access point`() {
        assertEquals("http://192.168.4.1", AtlasEndpoint.DEFAULT.baseUrl)
    }

    @Test
    fun `accepts bare hosts, trailing slashes and ports`() {
        assertEquals("http://192.168.4.1", AtlasEndpoint.parse("192.168.4.1").getOrThrow().baseUrl)
        assertEquals("http://192.168.4.1", AtlasEndpoint.parse(" http://192.168.4.1/ ").getOrThrow().baseUrl)
        assertEquals("http://atlas.local:8080", AtlasEndpoint.parse("HTTP://Atlas.local:8080").getOrThrow().baseUrl)
    }

    @Test
    fun `rejects anything that is not a bare http origin`() {
        listOf("", "   ", "https://192.168.4.1", "ftp://x", "http://192.168.4.1/api", "http://bad host", "http://h?q=1")
            .forEach { text ->
                val failure = (AtlasEndpoint.parse(text).exceptionOrNull() as? AtlasException)?.failure
                assertTrue("'$text' -> $failure", failure is AtlasFailure.InvalidEndpoint)
            }
    }

    @Test
    fun `fixture info is compatible`() {
        AtlasCompatibility.requireCompatible(Fixtures.info())
        AtlasCompatibility.requireCompatible(Fixtures.state("running.response.json"))
    }

    @Test
    fun `incompatible API, protocol or missing snapshots are rejected`() {
        val incompatible = listOf(
            Fixtures.info { put("apiVersion", "2") },
            Fixtures.info { put("protocolVersion", "0.2") },
            Fixtures.info { getJSONObject("capabilities").put("stateSnapshot", false) },
        )
        incompatible.forEach { info ->
            val error = assertThrows(AtlasException::class.java) { AtlasCompatibility.requireCompatible(info) }
            assertTrue(error.failure is AtlasFailure.Incompatible)
        }
        val error = assertThrows(AtlasException::class.java) {
            AtlasCompatibility.requireCompatible(Fixtures.state("running.response.json") { put("protocolVersion", "0.2") })
        }
        assertTrue(error.failure is AtlasFailure.Incompatible)
    }

    @Test
    fun `radio protocol version is not an Android concern`() {
        AtlasCompatibility.requireCompatible(Fixtures.info { put("radioProtocolVersion", 2) })
    }
}
