package com.turnhub.android.data

import com.turnhub.android.testing.Fixtures
import com.turnhub.android.protocol.AccessibilitySettings
import com.turnhub.android.protocol.LedStyle
import com.turnhub.android.protocol.LoginResult
import com.turnhub.android.protocol.ProfileSummary
import com.turnhub.android.protocol.SessionInfo
import kotlinx.coroutines.test.runTest
import kotlinx.coroutines.async
import kotlinx.coroutines.cancelAndJoin
import kotlinx.coroutines.test.runCurrent
import kotlinx.coroutines.test.advanceUntilIdle
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

@OptIn(kotlinx.coroutines.ExperimentalCoroutinesApi::class)
class AtlasPhonePairingTest {
    private val endpoint = AtlasEndpoint.parse("https://atlas.local").getOrThrow()

    @Test fun cancellingTheOverlayCancelsTheWaitingAtlasRequestWithoutSavingAKey() = runTest {
        val atlas=PairingAtlas(mutableListOf())
        val keys=MemoryAtlasPhoneKeyStore()
        val pairing=AtlasPhonePairing(keys,{atlas},pollMs=1000)
        val job=async { pairing.pair(endpoint,"THA-1","Test") }
        runCurrent();assertTrue(pairing.state.value is AtlasPhonePairing.State.Asking)
        job.cancelAndJoin()
        assertEquals(listOf("0123456789abcdef"),atlas.cancelled)
        assertFalse(pairing.paired("THA-1"));assertEquals(AtlasPhonePairing.State.Idle,pairing.state.value)
    }
    @Test fun firstHomeApprovalDisplaysTheActualPinnedKeyAndDoesNotReuseAnOldPhoneKey() = runTest {
        val atlas = PairingAtlas(mutableListOf("""{"state":"allowed","phone":"0a0b0c0d","key":"key"}"""))
        val keys = MemoryAtlasPhoneKeyStore().apply { save("THA-1","old-phone-key") }
        val pairing = AtlasPhonePairing(keys,{atlas},pollMs=1)
        val job=async { pairing.pair(AtlasEndpoint.home("192.168.1.40",443,"a".repeat(64)),
            "THA-1","Test",requireApproval=true) }
        runCurrent()
        assertEquals(AtlasPhonePairing.State.Asking("AAAA AAAA AAAA", true, false),pairing.state.value)
        assertNull(keys.tlsPin("THA-1"))
        pairing.confirmFingerprint()
        advanceUntilIdle();assertTrue(job.await())
        assertEquals("a".repeat(64), keys.tlsPin("THA-1"))
    }
    @Test fun failedKeyStorageDoesNotReportPairingSuccess() = runTest {
        val atlas = PairingAtlas(mutableListOf("""{"state":"allowed","phone":"0a0b0c0d","key":"key"}"""))
        val keys = object : AtlasPhoneKeyStore {
            override fun key(atlasId: String): String? = null
            override fun save(atlasId: String, credential: String) { error("Storage unavailable") }
            override fun forget(atlasId: String) {}
        }
        val pairing = AtlasPhonePairing(keys, { atlas }, pollMs = 1)
        assertFalse(pairing.pair(endpoint, "THA-1", "Fold"))
        assertFalse(pairing.paired("THA-1"))
        assertTrue(pairing.state.value is AtlasPhonePairing.State.Failed)
    }

    @Test
    fun firstSetupTrustRequiresPhoneConfirmationBeforeSavingThePinOrPairingKey() = runTest {
        val pin = "b".repeat(64)
        val atlas = PairingAtlas(
            mutableListOf("""{"state":"allowed","phone":"0a0b0c0d","key":"key"}"""),
            peerPin = pin,
        )
        val keys = MemoryAtlasPhoneKeyStore()
        val pairing = AtlasPhonePairing(keys, { atlas }, pollMs = 1)
        val job = async { pairing.trustSetupEndpoint(AtlasEndpoint.DEFAULT, "Fold") }

        runCurrent()
        val asking = pairing.state.value as AtlasPhonePairing.State.Asking
        assertEquals("BBBB BBBB BBBB", asking.code)
        assertTrue(asking.requiresFingerprintConfirmation)
        assertNull(keys.key("THA-025448410001"))
        assertNull(keys.tlsPin("THA-025448410001"))

        pairing.confirmFingerprint()
        advanceUntilIdle()
        val trusted = job.await()!!
        assertEquals(pin, trusted.endpoint.tlsPin)
        assertFalse(trusted.endpoint.bootstrapTls)
        assertEquals("phone:0a0b0c0d:key", keys.key(trusted.atlasId))
        assertEquals(pin, keys.tlsPin(trusted.atlasId))
    }

    @Test
    fun changedSetupKeyIsRejectedWithoutStartingPairing() = runTest {
        val atlas = PairingAtlas(mutableListOf(), peerPin = "b".repeat(64))
        val keys = MemoryAtlasPhoneKeyStore().apply {
            save("THA-025448410001", "existing-key", "a".repeat(64))
        }
        val pairing = AtlasPhonePairing(keys, { atlas })

        assertNull(pairing.trustSetupEndpoint(AtlasEndpoint.DEFAULT, "Fold"))
        assertTrue(pairing.state.value is AtlasPhonePairing.State.Failed)
        assertEquals(0, atlas.pairRequests)
        assertEquals("existing-key", keys.key("THA-025448410001"))
    }

    @Test
    fun setupTrustRetriesWhileTheAtlasTlsListenerStarts() = runTest {
        val pin = "c".repeat(64)
        val atlas = PairingAtlas(
            mutableListOf(),
            peerPin = pin,
            infoFailures = mutableListOf(AtlasFailure.Unreachable("refused"), AtlasFailure.Unreachable("refused")),
        )
        val keys = MemoryAtlasPhoneKeyStore().apply {
            save("THA-025448410001", "existing-key", pin)
        }

        val trusted = AtlasPhonePairing(keys, { atlas }).trustSetupEndpoint(AtlasEndpoint.DEFAULT, "Fold")

        assertEquals(pin, trusted?.endpoint?.tlsPin)
        assertEquals(0, atlas.pairRequests)
    }

    private class PairingAtlas(
        var answers: MutableList<String>,
        private val peerPin: String? = null,
        private val infoFailures: MutableList<AtlasFailure> = mutableListOf(),
    ) : AtlasSessionTransport {
        val cancelled=mutableListOf<String>()
        val tokens = mutableListOf<String>()
        var pairRequests = 0
        override val tlsPeerPin: String? get() = peerPin
        override suspend fun getInfo(): com.turnhub.android.protocol.AtlasInfo {
            if (infoFailures.isNotEmpty()) throw AtlasException(infoFailures.removeAt(0))
            return Fixtures.info()
        }
        override suspend fun getProfiles(): List<ProfileSummary> = emptyList()
        override suspend fun login(profileId: String, pin: String): LoginResult = throw UnsupportedOperationException()
        override suspend fun me(token: String): SessionInfo = throw UnsupportedOperationException()
        override suspend fun getAccessibility(token: String): AccessibilitySettings = throw UnsupportedOperationException()
        override suspend fun saveAccessibility(token: String, sigilSound: Boolean, ledStyle: LedStyle, longPressMs: Int,
                                               winHoldMs: Int, lifeApprovalMs: Int): AccessibilitySettings = throw UnsupportedOperationException()
        override suspend fun logout(token: String) {}
        override suspend fun raw(method: String, path: String, token: String, fields: List<Pair<String, String>>): RawResponse {
            if(path=="/api/phones/pair/cancel") { cancelled+=fields.toMap().getValue("request");return RawResponse(200,"{\"ok\":true}") }
            if (path == "/api/phones/pair" && method == "POST") {
                pairRequests++
                return RawResponse(200, """{"request":"0123456789abcdef","code":"123 456","waitMs":60000}""")
            }
            if (path == "/api/phones/pair") return RawResponse(200, answers.removeAt(0))
            tokens += token
            return RawResponse(if (token == "phone:0a0b0c0d:key") 200 else 401, "{}")
        }
    }

    @Test
    fun pairsOnceThenHostsWithTheKey() = runTest {
        val atlas = PairingAtlas(mutableListOf("""{"state":"waiting"}""",
            """{"state":"allowed","phone":"0a0b0c0d","key":"key","owner":true}"""))
        val keys = MemoryAtlasPhoneKeyStore()
        val pairing = AtlasPhonePairing(keys, { atlas }, pollMs = 1)
        assertFalse(pairing.paired("THA-1"))
        assertTrue(pairing.pair(endpoint, "THA-1", "Fold"))
        assertEquals(AtlasPhonePairing.State.Paired, pairing.state.value)
        assertEquals("phone:0a0b0c0d:key", keys.key("THA-1"))
        assertEquals(200, pairing.raw(endpoint, "THA-1", "POST", "/api/hosted/claim", emptyList())?.code)
        assertNull(pairing.raw(endpoint, "THA-2", "POST", "/api/hosted/claim", emptyList()))
    }

    @Test
    fun refusedAndForgottenPhonesHaveNoKey() = runTest {
        val atlas = PairingAtlas(mutableListOf("""{"state":"refused"}"""))
        val keys = MemoryAtlasPhoneKeyStore()
        val pairing = AtlasPhonePairing(keys, { atlas }, pollMs = 1)
        assertFalse(pairing.pair(endpoint, "THA-1", "Fold"))
        assertTrue(pairing.state.value is AtlasPhonePairing.State.Failed)
        // Atlas forgot this phone: a 401 drops the saved key.
        keys.save("THA-1", "phone:ffffffff:old")
        assertEquals(401, pairing.raw(endpoint, "THA-1", "POST", "/api/hosted/claim", emptyList())?.code)
        assertFalse(pairing.paired("THA-1"))
    }
}
