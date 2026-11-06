package com.turnhub.android.data

import com.turnhub.android.protocol.AtlasConnectionState.CONNECTED
import com.turnhub.android.protocol.AtlasConnectionState.CONNECTING
import com.turnhub.android.protocol.AtlasConnectionState.DISCONNECTED
import com.turnhub.android.protocol.AtlasInfo
import com.turnhub.android.protocol.StateSnapshot
import com.turnhub.android.protocol.TableState
import com.turnhub.android.testing.Fixtures
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.launch
import kotlinx.coroutines.test.TestScope
import kotlinx.coroutines.test.advanceTimeBy
import kotlinx.coroutines.test.runCurrent
import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertSame
import org.junit.Assert.assertTrue
import org.junit.Test

/** A scripted [AtlasTransport]: each call runs the current handler, which may throw. */
private class FakeAtlasTransport : AtlasTransport {
    var info: suspend () -> AtlasInfo = { Fixtures.info() }
    var state: suspend () -> StateSnapshot = { Fixtures.state("state-hosted.response.json") }
    var infoCalls = 0
    var stateCalls = 0

    override suspend fun getInfo(): AtlasInfo {
        infoCalls++
        return info()
    }

    override suspend fun getState(): StateSnapshot {
        stateCalls++
        return state()
    }
}

@OptIn(ExperimentalCoroutinesApi::class)
class HttpAtlasRepositoryTest {

    private val transport = FakeAtlasTransport()
    private val timeout = AtlasFailure.Timeout("SocketTimeoutException: test")
    private val endpoints = mutableListOf<AtlasEndpoint>()

    private fun TestScope.repository(scope: CoroutineScope = backgroundScope) = HttpAtlasRepository(
        transportFactory = { endpoint -> endpoints += endpoint; transport },
        scope = scope,
        pollIntervalMs = 1_000,
        maxConsecutivePollFailures = 3,
    )

    private fun TestScope.advance(ms: Long) {
        advanceTimeBy(ms)
        runCurrent()
    }

    private fun fail(failure: AtlasFailure): Nothing = throw AtlasException(failure)

    @Test
    fun `starts disconnected with nothing to show`() = runTest {
        val repository = repository()
        assertEquals(DISCONNECTED, repository.connectionState.value)
        assertNull(repository.tableSummary.value)
        assertNull(repository.failure.value)
    }

    @Test
    fun `background polling makes six requests in thirty seconds while local clocks remain on the phone`() = runTest {
        val repository = repository()
        repository.setPresentationForeground(false)
        repository.connect(AtlasEndpoint.DEFAULT)
        val initial = transport.stateCalls
        advance(30_000)
        assertEquals(6, transport.stateCalls - initial)
        repository.setPresentationForeground(true)
        advance(6_000)
        assertEquals(initial + 13, transport.stateCalls) // Immediate refresh, then one per second.
    }

    @Test
    fun `connect validates info, loads state, then reports connected`() = runTest {
        val repository = repository()

        repository.connect(AtlasEndpoint.DEFAULT)

        assertEquals(CONNECTED, repository.connectionState.value)
        assertEquals(listOf(AtlasEndpoint.DEFAULT), endpoints)
        assertEquals(1, transport.infoCalls)
        assertEquals(1, transport.stateCalls)
        val summary = repository.tableSummary.value!!
        assertEquals("THA-025448410001", summary.atlasId)
        assertEquals("0.6.0-dev", summary.firmwareVersion)
        assertEquals(7L, summary.revision)
        assertNull(repository.failure.value)
    }

    @Test
    fun `is not connected until the state snapshot has arrived`() = runTest {
        val stateGate = CompletableDeferred<StateSnapshot>()
        transport.state = { stateGate.await() }
        val repository = repository()

        val connecting = launch { repository.connect(AtlasEndpoint.DEFAULT) }
        runCurrent()
        assertEquals(CONNECTING, repository.connectionState.value)
        assertEquals(1, transport.infoCalls)
        assertNull(repository.tableSummary.value)

        stateGate.complete(Fixtures.state("state-hosted.response.json"))
        connecting.join()
        assertEquals(CONNECTED, repository.connectionState.value)
    }

    @Test
    fun `network failure while connecting leaves a useful error`() = runTest {
        transport.info = { fail(timeout) }
        val repository = repository()

        repository.connect(AtlasEndpoint.DEFAULT)

        assertEquals(DISCONNECTED, repository.connectionState.value)
        assertSame(timeout, repository.failure.value)
        assertNull(repository.tableSummary.value)
        assertEquals(0, transport.stateCalls)
    }

    @Test
    fun `incompatible responder is rejected before state is requested`() = runTest {
        transport.info = { Fixtures.info { put("apiVersion", "2") } }
        val repository = repository()

        repository.connect(AtlasEndpoint.DEFAULT)

        assertEquals(DISCONNECTED, repository.connectionState.value)
        assertTrue(repository.failure.value is AtlasFailure.Incompatible)
        assertEquals(0, transport.stateCalls)
    }

    @Test
    fun `non-TurnHub responder is rejected`() = runTest {
        transport.info = { fail(AtlasFailure.NotTurnHub("Responder is not a TurnHub Atlas")) }
        val repository = repository()

        repository.connect(AtlasEndpoint.DEFAULT)

        assertTrue(repository.failure.value is AtlasFailure.NotTurnHub)
        assertEquals(DISCONNECTED, repository.connectionState.value)
    }

    @Test
    fun `unexpected transport exceptions become failures, not crashes`() = runTest {
        transport.state = { throw IllegalStateException("boom") }
        val repository = repository()

        repository.connect(AtlasEndpoint.DEFAULT)

        assertTrue(repository.failure.value is AtlasFailure.Unexpected)
        assertEquals(DISCONNECTED, repository.connectionState.value)
    }

    @Test
    fun `polls state about once per second and renders each snapshot`() = runTest {
        val repository = repository()
        repository.connect(AtlasEndpoint.DEFAULT)

        transport.state = { Fixtures.state("state-hosted.response.json") { put("revision", 9) } }
        advance(999)
        assertEquals(1, transport.stateCalls)
        advance(1)
        assertEquals(2, transport.stateCalls)
        assertEquals(9L, repository.tableSummary.value!!.revision)
        assertEquals("Kitchen tablet", repository.tableSummary.value!!.hub?.name)

        advance(3_000)
        assertEquals(5, transport.stateCalls)
        assertEquals(1, transport.infoCalls)
    }

    @Test
    fun `same revision still refreshes sampled battery and links`() = runTest {
        val repository = repository()
        repository.connect(AtlasEndpoint.DEFAULT)

        transport.state = { Fixtures.state("state-hosted.response.json") { getJSONObject("battery").put("percent", 50) } }
        advance(1_000)

        val summary = repository.tableSummary.value!!
        assertEquals(7L, summary.revision)
        assertEquals(50, summary.battery?.percent)
    }

    @Test
    fun `disconnect stops polling and clears live state`() = runTest {
        val repository = repository()
        repository.connect(AtlasEndpoint.DEFAULT)
        advance(1_000)
        assertEquals(2, transport.stateCalls)

        repository.disconnect()
        advance(10_000)

        assertEquals(DISCONNECTED, repository.connectionState.value)
        assertNull(repository.tableSummary.value)
        assertNull(repository.failure.value)
        assertEquals(2, transport.stateCalls)
    }

    @Test
    fun `a response that lands after disconnect is discarded`() = runTest {
        val infoGate = CompletableDeferred<AtlasInfo>()
        transport.info = { infoGate.await() }
        val repository = repository()

        val connecting = launch { repository.connect(AtlasEndpoint.DEFAULT) }
        runCurrent()
        repository.disconnect()
        infoGate.complete(Fixtures.info())
        runCurrent()
        connecting.join()
        advance(5_000)

        assertEquals(DISCONNECTED, repository.connectionState.value)
        assertNull(repository.tableSummary.value)
        assertEquals(0, transport.stateCalls)
    }

    @Test
    fun `reconnect fetches a fresh snapshot without duplicate polling`() = runTest {
        val repository = repository()
        repository.connect(AtlasEndpoint.DEFAULT)
        repository.connect(AtlasEndpoint.DEFAULT) // Already connected: ignored.
        advance(1_000)
        assertEquals(2, transport.stateCalls)

        repository.disconnect()
        transport.state = { Fixtures.state("state-hosted.response.json") { put("revision", 9) } }
        repository.connect(AtlasEndpoint.DEFAULT)

        assertEquals(2, transport.infoCalls)
        assertEquals(3, transport.stateCalls)
        assertEquals(9L, repository.tableSummary.value!!.revision)

        advance(3_000)
        // One loop only: three polls in three seconds, not six.
        assertEquals(6, transport.stateCalls)
        assertEquals(listOf(AtlasEndpoint.DEFAULT, AtlasEndpoint.DEFAULT), endpoints)
    }

    @Test
    fun `changed boot ID rebuilds from fresh info and state`() = runTest {
        val repository = repository()
        repository.connect(AtlasEndpoint.DEFAULT)
        assertEquals(7L, repository.tableSummary.value!!.revision)

        // Atlas rebooted (e.g. OTA to new firmware) and recovered the match paused.
        transport.info = {
            Fixtures.info { put("bootId", Fixtures.OTHER_BOOT_ID).put("firmwareVersion", "0.6.1-dev") }
        }
        transport.state = {
            Fixtures.state("state-hosted.response.json") {
                put("bootId", Fixtures.OTHER_BOOT_ID).put("revision", 0)
                getJSONObject("hub").put("state", "PAUSED")
            }
        }
        advance(1_000)

        assertEquals(2, transport.infoCalls)
        assertEquals(CONNECTED, repository.connectionState.value)
        val summary = repository.tableSummary.value!!
        assertEquals(Fixtures.OTHER_BOOT_ID, summary.bootId)
        assertEquals(0L, summary.revision) // Lower revision accepted in the new epoch.
        assertEquals("0.6.1-dev", summary.firmwareVersion)
        assertEquals(TableState.PAUSED, summary.hub?.state)
    }

    @Test
    fun `revision going backwards in the same boot also rebuilds`() = runTest {
        val repository = repository()
        transport.state = { Fixtures.state("state-hosted.response.json") { put("revision", 9) } }
        repository.connect(AtlasEndpoint.DEFAULT)

        transport.state = { Fixtures.state("state-hosted.response.json") }
        advance(1_000)

        assertEquals(2, transport.infoCalls)
        assertEquals(7L, repository.tableSummary.value!!.revision)
    }

    @Test
    fun `a different Atlas at the same address replaces the view`() = runTest {
        val repository = repository()
        repository.connect(AtlasEndpoint.DEFAULT)

        transport.info = { Fixtures.info { put("atlasId", "THA-AAAAAAAAAAAA") } }
        transport.state = { Fixtures.state("state-idle.response.json") { put("atlasId", "THA-AAAAAAAAAAAA") } }
        advance(1_000)

        val summary = repository.tableSummary.value!!
        assertEquals("THA-AAAAAAAAAAAA", summary.atlasId)
        assertEquals(TableState.LOBBY, summary.state)
        assertTrue(summary.players.isEmpty())
    }

    @Test
    fun `one failed poll retries quietly and preserves the connected presentation`() = runTest {
        val repository = repository()
        repository.connect(AtlasEndpoint.DEFAULT)

        transport.state = { fail(timeout) }
        advance(1_000)
        assertEquals(CONNECTED, repository.connectionState.value)
        assertNull(repository.failure.value)
        assertNotNull(repository.tableSummary.value)

        transport.state = { Fixtures.state("state-hosted.response.json") { put("revision", 9) } }
        advance(1_000)
        assertNull(repository.failure.value)
        assertEquals(9L, repository.tableSummary.value!!.revision)
    }

    @Test
    fun `Atlas disappearing during polling keeps the last state offline`() = runTest {
        val repository = repository()
        repository.connect(AtlasEndpoint.DEFAULT)
        val lastKnown = repository.tableSummary.value

        transport.state = { fail(AtlasFailure.Unreachable("connection refused or no route")) }
        advance(2_000)
        assertNull(repository.offlineSinceMs.value)
        advance(1_000)

        assertEquals(CONNECTED, repository.connectionState.value)
        assertNotNull(repository.offlineSinceMs.value)
        assertSame(lastKnown, repository.tableSummary.value)
        val failure = repository.failure.value
        assertTrue(failure is AtlasFailure.LostConnection)
        assertTrue((failure as AtlasFailure.LostConnection).cause is AtlasFailure.Unreachable)

        // Offline it keeps trying, more gently.
        val callsWhenOffline = transport.stateCalls
        advance(10_000)
        assertEquals(callsWhenOffline + 5, transport.stateCalls)
        assertSame(lastKnown, repository.tableSummary.value)

        // The same boot answers again: back online with fresh state.
        transport.state = { Fixtures.state("state-hosted.response.json") { put("revision", 9) } }
        advance(2_000)
        assertEquals(CONNECTED, repository.connectionState.value)
        assertNull(repository.offlineSinceMs.value)
        assertNull(repository.failure.value)
        assertEquals(9L, repository.tableSummary.value!!.revision)
    }

    @Test
    fun `an Atlas that restarted while offline is rebuilt from a fresh handshake`() = runTest {
        val repository = repository()
        repository.connect(AtlasEndpoint.DEFAULT)

        transport.state = { fail(AtlasFailure.Unreachable("connection refused or no route")) }
        advance(3_000)
        assertNotNull(repository.offlineSinceMs.value)

        transport.info = { Fixtures.info { put("bootId", Fixtures.OTHER_BOOT_ID) } }
        transport.state = { Fixtures.state("state-hosted.response.json") { put("bootId", Fixtures.OTHER_BOOT_ID) } }
        advance(2_000)
        assertEquals(CONNECTED, repository.connectionState.value)
        assertNull(repository.offlineSinceMs.value)
        assertEquals(Fixtures.OTHER_BOOT_ID, repository.tableSummary.value!!.bootId)
    }

    @Test
    fun `disconnecting while offline clears the last state`() = runTest {
        val repository = repository()
        repository.connect(AtlasEndpoint.DEFAULT)
        transport.state = { fail(timeout) }
        advance(3_000)
        assertNotNull(repository.offlineSinceMs.value)

        repository.disconnect()
        assertEquals(DISCONNECTED, repository.connectionState.value)
        assertNull(repository.tableSummary.value)
        assertNull(repository.offlineSinceMs.value)
    }

    @Test
    fun `holding keeps the connection through a busy Atlas`() = runTest {
        val repository = repository()
        repository.connect(AtlasEndpoint.DEFAULT)
        repository.holdThroughOutages(true)

        transport.state = { fail(timeout) }
        advance(10_000)
        assertEquals(CONNECTED, repository.connectionState.value)
        assertNull(repository.failure.value)
        assertNotNull(repository.tableSummary.value)

        transport.state = { Fixtures.state("state-hosted.response.json") { put("revision", 9) } }
        advance(1_000)
        assertNull(repository.failure.value)

        assertNull(repository.offlineSinceMs.value)

        // Released, the usual limit applies again.
        repository.holdThroughOutages(false)
        transport.state = { fail(timeout) }
        advance(3_000)
        assertNotNull(repository.offlineSinceMs.value)
    }

    @Test
    fun `a boot change between info and state is retried once`() = runTest {
        var infoBoot = Fixtures.BOOT_ID
        transport.info = { Fixtures.info { put("bootId", infoBoot) } }
        transport.state = {
            infoBoot = Fixtures.OTHER_BOOT_ID // Atlas restarts after answering info.
            Fixtures.state("state-hosted.response.json") { put("bootId", Fixtures.OTHER_BOOT_ID) }
        }
        val repository = repository()

        repository.connect(AtlasEndpoint.DEFAULT)

        assertEquals(CONNECTED, repository.connectionState.value)
        assertEquals(2, transport.infoCalls)
        assertEquals(Fixtures.OTHER_BOOT_ID, repository.tableSummary.value!!.bootId)
    }

    @Test
    fun `summaries are stamped with the local receive time`() = runTest {
        var now = 10_000L
        val repository = HttpAtlasRepository(
            transportFactory = { transport },
            scope = backgroundScope,
            clock = { now },
        )
        repository.connect(AtlasEndpoint.DEFAULT)
        assertEquals(10_000L, repository.tableSummary.value!!.receivedAtMs)

        now = 11_000L
        advance(1_000)
        assertEquals(11_000L, repository.tableSummary.value!!.receivedAtMs)
    }

    @Test
    fun `canceling the owning scope stops polling`() = runTest {
        val owner = CoroutineScope(coroutineContext + kotlinx.coroutines.Job())
        val repository = repository(owner)
        repository.connect(AtlasEndpoint.DEFAULT)

        owner.coroutineContext[kotlinx.coroutines.Job]!!.cancel()
        advance(5_000)

        assertEquals(1, transport.stateCalls)
    }
    @Test fun `slow failed poll marks stale data offline without waiting for three long requests`() = runTest {
        val repository = HttpAtlasRepository({ transport }, backgroundScope, clock = { testScheduler.currentTime })
        repository.connect(AtlasEndpoint.DEFAULT)
        transport.state = { kotlinx.coroutines.delay(11_000); fail(timeout) }
        advance(12_000)
        assertNotNull(repository.offlineSinceMs.value)
        assertTrue(repository.failure.value is AtlasFailure.LostConnection)
        assertNotNull(repository.tableSummary.value)
    }

    @Test fun `TLS identity errors are visible on the first bad poll`() = runTest {
        val repository = repository(); repository.connect(AtlasEndpoint.DEFAULT)
        val identity = AtlasFailure.TlsIdentity("different key")
        transport.state = { fail(identity) }
        advance(1_000)
        assertSame(identity, repository.failure.value)
    }

    @Test fun `a transient busy response does not replace the last good presentation`() = runTest {
        val repository = repository(); repository.connect(AtlasEndpoint.DEFAULT)
        val before = repository.tableSummary.value
        transport.state = { fail(AtlasFailure.HttpStatus(503, "busy")) }
        advance(2_000)
        assertNull(repository.failure.value)
        assertNull(repository.offlineSinceMs.value)
        assertSame(before, repository.tableSummary.value)
        advance(1_000)
        assertTrue(repository.failure.value is AtlasFailure.LostConnection)
    }

}
