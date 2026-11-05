package com.turnhub.android.data

import com.turnhub.android.testing.Fixtures
import org.junit.Assert.assertEquals
import org.junit.Assert.assertThrows
import org.junit.Assert.assertTrue
import org.junit.Test

class AtlasEndpointAndCompatibilityTest {

    @Test
    fun `default endpoint is the Atlas access point`() {
        assertEquals("https://192.168.4.1", AtlasEndpoint.DEFAULT.baseUrl)
        assertTrue(AtlasEndpoint.DEFAULT.bootstrapTls)
    }

    @Test
    fun `accepts bare hosts and HTTPS origins`() {
        assertEquals("https://192.168.4.1", AtlasEndpoint.parse("192.168.4.1").getOrThrow().baseUrl)
        assertEquals("https://192.168.4.1", AtlasEndpoint.parse(" https://192.168.4.1/ ").getOrThrow().baseUrl)
        assertEquals("https://atlas.local:8080", AtlasEndpoint.parse("HTTPS://Atlas.local:8080").getOrThrow().baseUrl)
    }

    @Test
    fun `rejects plaintext HTTP and anything that is not a bare HTTPS origin`() {
        listOf("", "   ", "http://192.168.4.1", "ftp://x", "https://192.168.4.1/api", "https://bad host", "https://h?q=1")
            .forEach { text ->
                val failure = (AtlasEndpoint.parse(text).exceptionOrNull() as? AtlasException)?.failure
                assertTrue("'$text' -> $failure", failure is AtlasFailure.InvalidEndpoint)
            }
    }

    @Test
    fun `fixture info is compatible`() {
        AtlasCompatibility.requireCompatible(Fixtures.info())
        AtlasCompatibility.requireCompatible(Fixtures.state("state-hosted.response.json"))
    }

    @Test
    fun `incompatible API, protocol or missing snapshots are rejected`() {
        val incompatible = listOf(
            Fixtures.info { put("apiVersion", "2") },
            Fixtures.info { put("protocolVersion", "2") },
            Fixtures.info { getJSONObject("capabilities").put("stateSnapshot", false) },
        )
        incompatible.forEach { info ->
            val error = assertThrows(AtlasException::class.java) { AtlasCompatibility.requireCompatible(info) }
            assertTrue(error.failure is AtlasFailure.Incompatible)
        }
        val error = assertThrows(AtlasException::class.java) {
            AtlasCompatibility.requireCompatible(Fixtures.state("state-hosted.response.json") { put("protocolVersion", "2") })
        }
        assertTrue(error.failure is AtlasFailure.Incompatible)
    }

    @Test
    fun `radio protocol version is not an Android concern`() {
        AtlasCompatibility.requireCompatible(Fixtures.info { put("radioProtocolVersion", 2) })
    }
}
