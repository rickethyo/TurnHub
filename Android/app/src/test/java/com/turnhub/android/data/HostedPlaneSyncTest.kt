package com.turnhub.android.data

import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.test.*
import org.junit.Assert.*
import org.junit.Test

@OptIn(ExperimentalCoroutinesApi::class)
class HostedPlaneSyncTest {
    private val card = PlaneInfo("00000000-0000-0000-0000-000000000001", "Test plane", "Plane - Test", "Rules", "Artist", "", "Test")

    @Test fun `quiet game recovers late catalog network and missing art without another roll`() = runTest {
        var catalog: List<PlaneInfo>? = null
        var online = false
        var art = false
        var storage = true
        var storedArt = false
        var cardSaved = false
        val bodies = mutableListOf<String>()
        val messages = mutableListOf<String?>()
        val sync = HostedPlaneSync({ _, _, _ ->
            if (!online) null else RawResponse(200, "{\"version\":\"\",\"cards\":${if (cardSaved) "[\"${card.id}\"]" else "[]"},\"art\":${if (storedArt) "[\"${card.id}\"]" else "[]"}}")
        }, { path, _, body ->
            if (path.endsWith("/card")) {
                bodies += body
                if (!storage) return@HostedPlaneSync RawResponse(409, "{}")
                cardSaved = true
            }
            RawResponse(202, "{\"queued\":true}")
        }, { catalog }, { if (art) byteArrayOf(1) else null }, retryMs = 100,
            encodeArt = { "encoded-art" }, report = { messages += it }, ioDispatcher = UnconfinedTestDispatcher(testScheduler))
        val job = launch { sync.run(MutableStateFlow(listOf(card.id))) }
        runCurrent()
        assertTrue(messages.last()!!.contains("download"))
        catalog = listOf(card)
        advanceTimeBy(101); runCurrent()
        assertTrue(messages.last()!!.contains("Retrying"))
        online = true
        advanceTimeBy(100); runCurrent()
        assertEquals(listOf(""), bodies) // Text is useful while artwork is unavailable.
        advanceTimeBy(100); runCurrent()
        assertEquals(1, bodies.size) // No repeated text-only upload over an already saved card.
        art = true
        storage = false
        advanceTimeBy(100); runCurrent()
        assertTrue(messages.last()!!.contains("microSD"))
        storage = true
        advanceTimeBy(100); runCurrent()
        assertEquals("encoded-art", bodies.last())
        assertFalse(messages.last()!!.contains("artwork saved")) // Queued does not mean durable.
        storedArt = true
        advanceTimeBy(100); runCurrent()
        assertEquals("Planes and artwork saved on Atlas.", messages.last())
        val sent = bodies.size
        storedArt = false; cardSaved = false // Atlas storage/reboot recovery, same desired plane.
        advanceTimeBy(100); runCurrent()
        assertEquals(sent + 1, bodies.size)
        job.cancel(); runCurrent()
        assertNull(messages.last())
    }

    @Test fun `busy storage retries and cancellation prevents sending a stale plane`() = runTest {
        var busy = true
        var reads = 0
        val sent = mutableListOf<String>()
        // A not-yet-downloaded upcoming card must not block the current plane.
        val wanted = MutableStateFlow(listOf(card.id, "00000000-0000-0000-0000-000000000002"))
        val sync = HostedPlaneSync({ _, _, _ ->
            reads++
            if (busy) RawResponse(503, "{}") else RawResponse(200, "{}")
        }, { path, _, _ -> sent += path; RawResponse(202, "{}") }, { listOf(card) }, { null },
            busyWaitMs = 100, retryMs = 100, ioDispatcher = UnconfinedTestDispatcher(testScheduler))
        val job = launch { sync.run(wanted) }
        runCurrent()
        assertTrue(sent.isEmpty())
        busy = false
        advanceTimeBy(101); runCurrent()
        assertTrue(reads >= 2)
        assertTrue(sent.contains("/api/planes/card"))
        busy = true
        advanceTimeBy(100); runCurrent()
        val count = sent.size
        wanted.value = emptyList(); runCurrent()
        busy = false
        advanceTimeBy(1000); runCurrent()
        assertEquals(count, sent.size)
        job.cancel()
    }
}
