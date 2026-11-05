package com.turnhub.android.data

import com.turnhub.android.protocol.AtlasHomeAddress
import com.turnhub.android.protocol.AtlasWireException
import com.turnhub.android.protocol.AtlasWireParser
import com.turnhub.android.testing.Fixtures
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertFalse
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertThrows
import org.junit.Assert.assertTrue
import org.junit.Test
import java.security.KeyPairGenerator
import java.security.MessageDigest
import java.security.cert.CertificateException

class AtlasHomeLinkTest {

    @Test fun `saved home link belongs only to that Atlas during setup reconnect`() {
        val store = MemoryHomeAtlasStore()
        store.save(SavedHomeAtlas("THA-cc68c78d84ac", "192.168.1.40", 443, "a".repeat(64)))
        val link = AtlasHomeLink(store, info = { error("not called") })
        assertTrue(link.hasSavedAtlas("THA-CC68C78D84AC"))
        assertFalse(link.hasSavedAtlas("THA-other"))
        assertFalse(link.hasSavedAtlas(null))
    }

    private val pin = "5f1c0b2a9d7e4c3b8a6f0e1d2c3b4a59687766554433221100ffeeddccbbaa99"

    @Test
    fun `fresh install discovers without hotspot but saves only after physical confirmation`() = runBlocking {
        val store = MemoryHomeAtlasStore()
        val candidate = SavedHomeAtlas("THA-cc68c78d84ac", "192.168.1.40", 443, pin)
        val asked = mutableListOf<AtlasEndpoint>()
        val link = AtlasHomeLink(store, info = { asked += it; Fixtures.info { put("atlasId", "THA-CC68C78D84AC") } }, discoverNew = { candidate })
        assertEquals(candidate, link.pairingCandidate())
        assertNull(store.last())
        assertEquals("5F1C-0B2A-9D7E", candidate.linkCode)
        assertEquals(candidate.endpoint, link.confirmPairing(candidate))
        assertEquals(candidate, store.last())
        assertEquals(candidate.endpoint, link.find())
        assertTrue(asked.all { it.secure && it.tlsPin == pin })
    }

    @Test
    fun `discovery never replaces saved trust and changed identity cannot be confirmed`() = runBlocking {
        val candidate = SavedHomeAtlas("THA-025448410001", "192.168.1.40", 443, pin)
        val store = MemoryHomeAtlasStore().apply { save(candidate) }
        val saved = AtlasHomeLink(store, info = { error("must not probe new tables") },
            discoverNew = { error("must not discover new tables") })
        assertNull(saved.pairingCandidate())
        assertNull(saved.confirmPairing(candidate.copy(pin = "a".repeat(64))))
        assertEquals(candidate, store.last())

        val fresh = MemoryHomeAtlasStore()
        var sameTable = true
        val link = AtlasHomeLink(fresh, info = {
            Fixtures.info { if (!sameTable) put("atlasId", "THA-OTHER") }
        }, discoverNew = { candidate })
        assertEquals(candidate, link.pairingCandidate())
        sameTable = false
        assertNull(link.confirmPairing(candidate))
        assertNull(fresh.last())
        assertNull(link.pairingCandidate())
    }

    @Test
    fun `info carries the home address and pin`() {
        val info = AtlasWireParser.parseInfo(Fixtures.text("info.response.json"))
        assertEquals(AtlasHomeAddress("192.168.1.40", 443, pin), info.home)
        assertNull(Fixtures.info { put("home", org.json.JSONObject.NULL) }.home)
        assertNull(Fixtures.info { remove("home") }.home)
        assertThrows(AtlasWireException::class.java) {
            Fixtures.info { getJSONObject("home").put("tlsPin", "not-a-pin") }
        }
    }

    @Test
    fun `home endpoints are https with the pin`() {
        val home = AtlasEndpoint.home("192.168.1.40", 443, pin.uppercase())
        assertEquals("https://192.168.1.40", home.baseUrl)
        assertEquals(pin, home.tlsPin)
        assertTrue(home.secure && !AtlasEndpoint.DEFAULT.secure)
        assertEquals("https://192.168.1.40:8443", AtlasEndpoint.home("192.168.1.40", 8443, pin).baseUrl)
    }

    @Test
    fun `learns only over the hotspot, then finds the pinned address`() = runBlocking {
        val store = MemoryHomeAtlasStore()
        val asked = mutableListOf<String>()
        val link = AtlasHomeLink(store, info = { endpoint ->
            asked += endpoint.baseUrl
            if (endpoint.secure && endpoint.host != "192.168.1.40") error("no answer")
            Fixtures.info()
        })
        link.learn(AtlasEndpoint.home("192.168.1.40", 443, pin))  // Never learned over the home network itself.
        assertNull(store.last())
        link.learn(AtlasEndpoint.DEFAULT)
        assertEquals(SavedHomeAtlas("THA-025448410001", "192.168.1.40", 443, pin), store.last())
        assertEquals(AtlasEndpoint.home("192.168.1.40", 443, pin), link.find())
        assertEquals("https://192.168.1.40", asked.last())
    }

    @Test
    fun `a moved Atlas is found by its key, another table is not used`() = runBlocking {
        val store = MemoryHomeAtlasStore().apply { save(SavedHomeAtlas("THA-025448410001", "192.168.1.9", 443, pin)) }
        val moved = AtlasHomeLink(store, info = { endpoint ->
            if (endpoint.host == "192.168.1.9") error("gone") else Fixtures.info()
        }, discover = { wanted -> if (wanted == pin) "192.168.1.77" to 443 else null })
        assertEquals("https://192.168.1.77", moved.find()?.baseUrl)
        assertEquals("192.168.1.77", store.last()?.address)

        val other = AtlasHomeLink(store, info = { Fixtures.info { put("atlasId", "THA-OTHER") } })
        assertNull(other.find())
        assertNull(AtlasHomeLink(MemoryHomeAtlasStore(), info = { Fixtures.info() }).find())
    }

    @Test
    fun `trusts only the pinned key`() {
        val generator = KeyPairGenerator.getInstance("EC").apply { initialize(256) }
        val key = generator.generateKeyPair().public
        val expected = MessageDigest.getInstance("SHA-256").digest(key.encoded).joinToString("") { "%02x".format(it) }
        assertEquals(expected, PinnedTls.pinOf(key))
        assertEquals(64, PinnedTls.pinOf(key).length)
        assertThrows(CertificateException::class.java) {
            PinnedTls.TrustManager(expected).checkServerTrusted(emptyArray(), "ECDHE_ECDSA")
        }
        assertThrows(CertificateException::class.java) {
            PinnedTls.TrustManager(expected).checkClientTrusted(emptyArray(), "ECDHE_ECDSA")
        }
    }
}
