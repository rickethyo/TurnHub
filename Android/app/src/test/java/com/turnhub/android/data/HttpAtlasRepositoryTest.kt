package com.turnhub.android.data

import com.turnhub.android.protocol.AtlasConnectionState.CONNECTED
import com.turnhub.android.protocol.AtlasConnectionState.CONNECTING
import com.turnhub.android.protocol.AtlasConnectionState.DISCONNECTED
import com.turnhub.android.protocol.AtlasInfo
import com.turnhub.android.protocol.SeatEntry
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
    var state: suspend () -> StateSnapshot = { Fixtures.state("running.response.json") }
    var seats: suspend () -> List<SeatEntry> = { emptyList() }
    var infoCalls = 0
    var stateCalls = 0
    var seatCalls = 0

    override suspend fun getInfo(): AtlasInfo {
        infoCalls++
        return info()
    }

    override suspend fun getState(): StateSnapshot {
        stateCalls++
        return state()
    }

    override suspend fun getSeats(): List<SeatEntry> {
        seatCalls++
        return seats()
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
        assertEquals(4L, summary.revision)
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

        stateGate.complete(Fixtures.state("running.response.json"))
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

        transport.state = { Fixtures.state("reconnected.response.json") }
        advance(999)
        assertEquals(1, transport.stateCalls)
        advance(1)
        assertEquals(2, transport.stateCalls)
        assertEquals(6L, repository.tableSummary.value!!.revision)
        assertEquals(2, repository.tableSummary.value!!.activePlayerNumber)

        advance(3_000)
        assertEquals(5, transport.stateCalls)
        assertEquals(1, transport.infoCalls)
    }

    @Test
    fun `same revision still refreshes sampled clocks`() = runTest {
        val repository = repository()
        repository.connect(AtlasEndpoint.DEFAULT)

        transport.state = { Fixtures.state("running.response.json") { put("turnElapsedMs", 1_100) } }
        advance(1_000)

        val summary = repository.tableSummary.value!!
        assertEquals(4L, summary.revision)
        assertEquals(1_100L, summary.turnElapsedMs)
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
        transport.state = { Fixtures.state("reconnected.response.json") }
        repository.connect(AtlasEndpoint.DEFAULT)

        assertEquals(2, transport.infoCalls)
        assertEquals(3, transport.stateCalls)
        assertEquals(6L, repository.tableSummary.value!!.revision)

        advance(3_000)
        // One loop only: three polls in three seconds, not six.
        assertEquals(6, transport.stateCalls)
        assertEquals(listOf(AtlasEndpoint.DEFAULT, AtlasEndpoint.DEFAULT), endpoints)
    }

    @Test
    fun `changed boot ID rebuilds from fresh info and state`() = runTest {
        val repository = repository()
        repository.connect(AtlasEndpoint.DEFAULT)
        assertEquals(4L, repository.tableSummary.value!!.revision)

        // Atlas rebooted (e.g. OTA to new firmware) and recovered the match paused.
        transport.info = {
            Fixtures.info { put("bootId", Fixtures.OTHER_BOOT_ID).put("firmwareVersion", "0.6.1-dev") }
        }
        transport.state = {
            Fixtures.state("running.response.json") {
                put("bootId", Fixtures.OTHER_BOOT_ID).put("revision", 0).put("state", "PAUSED")
            }
        }
        advance(1_000)

        assertEquals(2, transport.infoCalls)
        assertEquals(CONNECTED, repository.connectionState.value)
        val summary = repository.tableSummary.value!!
        assertEquals(Fixtures.OTHER_BOOT_ID, summary.bootId)
        assertEquals(0L, summary.revision) // Lower revision accepted in the new epoch.
        assertEquals("0.6.1-dev", summary.firmwareVersion)
        assertEquals(TableState.PAUSED, summary.state)
    }

    @Test
    fun `revision going backwards in the same boot also rebuilds`() = runTest {
        val repository = repository()
        transport.state = { Fixtures.state("reconnected.response.json") }
        repository.connect(AtlasEndpoint.DEFAULT)

        transport.state = { Fixtures.state("running.response.json") }
        advance(1_000)

        assertEquals(2, transport.infoCalls)
        assertEquals(4L, repository.tableSummary.value!!.revision)
    }

    @Test
    fun `a different Atlas at the same address replaces the view`() = runTest {
        val repository = repository()
        repository.connect(AtlasEndpoint.DEFAULT)

        transport.info = { Fixtures.info { put("atlasId", "THA-AAAAAAAAAAAA") } }
        transport.state = { Fixtures.state("lobby.response.json") { put("atlasId", "THA-AAAAAAAAAAAA") } }
        advance(1_000)

        val summary = repository.tableSummary.value!!
        assertEquals("THA-AAAAAAAAAAAA", summary.atlasId)
        assertEquals(TableState.LOBBY, summary.state)
        assertTrue(summary.players.isEmpty())
    }

    @Test
    fun `one failed poll is retried and cleared`() = runTest {
        val repository = repository()
        repository.connect(AtlasEndpoint.DEFAULT)

        transport.state = { fail(timeout) }
        advance(1_000)
        assertEquals(CONNECTED, repository.connectionState.value)
        assertSame(timeout, repository.failure.value)
        assertNotNull(repository.tableSummary.value)

        transport.state = { Fixtures.state("reconnected.response.json") }
        advance(1_000)
        assertNull(repository.failure.value)
        assertEquals(6L, repository.tableSummary.value!!.revision)
    }

    @Test
    fun `Atlas disappearing during polling drops the live view`() = runTest {
        val repository = repository()
        repository.connect(AtlasEndpoint.DEFAULT)

        transport.state = { fail(AtlasFailure.Unreachable("connection refused or no route")) }
        advance(3_000)

        assertEquals(DISCONNECTED, repository.connectionState.value)
        assertNull(repository.tableSummary.value)
        val failure = repository.failure.value
        assertTrue(failure is AtlasFailure.LostConnection)
        assertTrue((failure as AtlasFailure.LostConnection).cause is AtlasFailure.Unreachable)

        val callsWhenDropped = transport.stateCalls
        advance(5_000)
        assertEquals(callsWhenDropped, transport.stateCalls)

        // Power-cycled Atlas is back: reconnecting starts from a fresh snapshot.
        transport.info = { Fixtures.info { put("bootId", Fixtures.OTHER_BOOT_ID) } }
        transport.state = { Fixtures.state("running.response.json") { put("bootId", Fixtures.OTHER_BOOT_ID) } }
        repository.connect(AtlasEndpoint.DEFAULT)
        assertEquals(CONNECTED, repository.connectionState.value)
        assertEquals(Fixtures.OTHER_BOOT_ID, repository.tableSummary.value!!.bootId)
        assertNull(repository.failure.value)
    }

    @Test
    fun `a boot change between info and state is retried once`() = runTest {
        var infoBoot = Fixtures.BOOT_ID
        transport.info = { Fixtures.info { put("bootId", infoBoot) } }
        transport.state = {
            infoBoot = Fixtures.OTHER_BOOT_ID // Atlas restarts after answering info.
            Fixtures.state("running.response.json") { put("bootId", Fixtures.OTHER_BOOT_ID) }
        }
        val repository = repository()

        repository.connect(AtlasEndpoint.DEFAULT)

        assertEquals(CONNECTED, repository.connectionState.value)
        assertEquals(2, transport.infoCalls)
        assertEquals(Fixtures.OTHER_BOOT_ID, repository.tableSummary.value!!.bootId)
    }

    @Test
    fun `players are named from seats by handle and slot`() = runTest {
        transport.seats = { listOf(SeatEntry(moduleId = 8, slot = 1, playerNumber = 1, name = "Ricky")) }
        val repository = repository()

        repository.connect(AtlasEndpoint.DEFAULT)

        val players = repository.tableSummary.value!!.players
        assertEquals(listOf("Ricky", "Player 2"), players.map { it.label })
        assertEquals(listOf(true, false), players.map { it.hasName })
    }

    @Test
    fun `a failing seats request never breaks the connection`() = runTest {
        transport.seats = { fail(timeout) }
        val repository = repository()

        repository.connect(AtlasEndpoint.DEFAULT)
        advance(3_000)

        assertEquals(CONNECTED, repository.connectionState.value)
        assertNull(repository.failure.value)
        assertEquals(listOf("Player 1", "Player 2"), repository.tableSummary.value!!.players.map { it.label })
    }

    @Test
    fun `names are kept when a later seats refresh fails`() = runTest {
        transport.seats = { listOf(SeatEntry(8, 1, 1, "Ricky")) }
        val repository = repository()
        repository.connect(AtlasEndpoint.DEFAULT)

        transport.seats = { fail(timeout) }
        transport.state = { Fixtures.state("reconnected.response.json") } // Revision change forces a refresh.
        advance(1_000)

        assertEquals("Ricky", repository.tableSummary.value!!.players[0].label)
    }

    @Test
    fun `names refresh on revision changes and at least every five polls`() = runTest {
        val repository = repository()
        repository.connect(AtlasEndpoint.DEFAULT)
        assertEquals(1, transport.seatCalls)

        advance(4_000) // Same revision: four polls, no refresh yet.
        assertEquals(1, transport.seatCalls)
        advance(1_000) // Fifth poll refreshes.
        assertEquals(2, transport.seatCalls)

        transport.state = { Fixtures.state("reconnected.response.json") }
        advance(1_000) // Revision changed: refresh immediately.
        assertEquals(3, transport.seatCalls)
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
    fun `cancelling the owning scope stops polling`() = runTest {
        val owner = CoroutineScope(coroutineContext + kotlinx.coroutines.Job())
        val repository = repository(owner)
        repository.connect(AtlasEndpoint.DEFAULT)

        owner.coroutineContext[kotlinx.coroutines.Job]!!.cancel()
        advance(5_000)

        assertEquals(1, transport.stateCalls)
    }
}
