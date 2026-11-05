package com.turnhub.android.data

import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.test.advanceTimeBy
import kotlinx.coroutines.test.runCurrent
import kotlinx.coroutines.test.runTest
import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

@OptIn(ExperimentalCoroutinesApi::class)
class HostedAtlasLinkTest {
    private val allCalls = mutableListOf<String>()
    /** The pushes and lease calls; input polls are checked on their own. */
    private val calls get() = allCalls.filter { it.startsWith("POST") }
    private var queued = "{\"inputs\":[],\"last\":0}"
    private var claimCode = 200
    private var stateCode = 200
    private var watched = "{\"live\":false,\"host\":null,\"game\":null,\"resume\":null}"

    private val link = HostedAtlasLink({ method, path, fields ->
        allCalls += "$method $path" + (fields.firstOrNull { it.first == "state" }?.let { " " + JSONObject(it.second).getLong("revision") } ?: "")
        when (path) {
            "/api/hosted/claim" -> RawResponse(claimCode, if (claimCode == 409) "{\"ok\":false,\"host\":\"Ana\"}" else "{\"ok\":true}")
            "/api/hosted/state" -> if (method == "GET") RawResponse(200, watched) else RawResponse(stateCode, "{\"ok\":true}")
            "/api/hosted/inputs" -> RawResponse(200, queued)
            else -> RawResponse(200, "{\"ok\":true}")
        }
    }, renewMs = 10_000)

    private fun game(revision: Long) = JSONObject().put("gameId", "g").put("revision", revision)

    @Test
    fun `restarting publication waits for the old lease release`() = runTest {
        val releasing = CompletableDeferred<Unit>()
        val released = CompletableDeferred<Unit>()
        val events = mutableListOf<String>()
        val serialLink = HostedAtlasLink({ _, path, _ ->
            if (path == "/api/hosted/release") {
                releasing.complete(Unit)
                released.await()
            }
            events += path
            RawResponse(200, "{\"ok\":true,\"inputs\":[],\"last\":0}")
        })
        val first = launch { serialLink.host(MutableStateFlow(game(1))) }
        runCurrent()
        first.cancel()
        runCurrent()
        assertTrue(releasing.isCompleted)
        val second = launch { serialLink.host(MutableStateFlow(game(2))) }
        runCurrent()
        assertEquals(1, events.count { it == "/api/hosted/claim" })
        released.complete(Unit)
        runCurrent()
        assertEquals(2, events.count { it == "/api/hosted/claim" })
        assertEquals(HostedAtlasLink.Status.Hosting, serialLink.status.value)
        second.cancel()
    }

    @Test
    fun `a failed first claim and a failed quiet push recover without another game edit`() = runTest {
        claimCode = 503
        val job = launch { link.host(MutableStateFlow(game(1))) }
        runCurrent()
        assertTrue(link.status.value is HostedAtlasLink.Status.Failed)
        claimCode = 200
        advanceTimeBy(10_001)
        assertEquals(HostedAtlasLink.Status.Hosting, link.status.value)
        stateCode = 503
        advanceTimeBy(10_001)
        assertTrue(link.status.value is HostedAtlasLink.Status.Failed)
        stateCode = 200
        advanceTimeBy(10_001)
        assertEquals(HostedAtlasLink.Status.Hosting, link.status.value)
        assertEquals("POST /api/hosted/state 1", calls.last())
        job.cancel()
    }

    @Test
    fun `claims, pushes each new state, renews while quiet and releases when stopped`() = runTest {
        val games = MutableStateFlow(game(1))
        val job = launch { link.host(games) }
        runCurrent()
        assertEquals(listOf("POST /api/hosted/claim", "POST /api/hosted/state 1"), calls)
        assertEquals(HostedAtlasLink.Status.Hosting, link.status.value)
        games.value = game(2)
        runCurrent()
        assertEquals("POST /api/hosted/state 2", calls.last())
        advanceTimeBy(10_001)
        assertEquals("POST /api/hosted/state 2", calls.last())
        assertEquals(4, calls.size)
        job.cancel()
        runCurrent()
        assertEquals("POST /api/hosted/release", calls.last())
        assertEquals(HostedAtlasLink.Status.Off, link.status.value)
    }

    @Test
    fun `another hub keeps the table and nothing is pushed`() = runTest {
        claimCode = 409
        link.host(MutableStateFlow(game(1)))
        assertEquals(HostedAtlasLink.Status.Busy("Ana"), link.status.value)
        assertTrue(calls.none { it.startsWith("POST /api/hosted/state") || it.endsWith("release") })
    }

    @Test
    fun `a lost lease is claimed back once`() = runTest {
        val job = launch { link.host(MutableStateFlow(game(1))) }
        runCurrent()
        stateCode = 409
        claimCode = 409
        advanceTimeBy(10_001)
        assertEquals(HostedAtlasLink.Status.Busy("Ana"), link.status.value)
        job.cancel()
        runCurrent()
        assertTrue(calls.none { it.endsWith("release") })
    }

    @Test
    fun `atlas inputs reach the rules once each`() = runTest {
        val applied = mutableListOf<HostedAtlasLink.Input>()
        val job = launch { link.host(MutableStateFlow(game(1))) { applied += it } }
        runCurrent()
        queued = "{\"inputs\":[{\"seq\":1,\"id\":\"a\",\"player\":2,\"action\":{\"kind\":\"pass\"}}],\"last\":1}"
        advanceTimeBy(301)
        advanceTimeBy(301)  // Polled again: the same input is not applied twice.
        assertEquals(1, applied.size)
        assertEquals(2, applied[0].player)
        assertEquals("pass", applied[0].action.getString("kind"))
        assertTrue(allCalls.any { it == "GET /api/hosted/inputs" })
        job.cancel()
    }

    @Test
    fun `offers the last hub's unfinished game to resume`() = runTest {
        assertEquals(null, link.resumable())
        watched = "{\"live\":false,\"host\":null,\"game\":null,\"resume\":{\"host\":\"Ana\",\"game\":{\"gameId\":\"g7\",\"revision\":5}}}"
        val resume = link.resumable()!!
        assertEquals("Ana", resume.host)
        assertEquals("g7", resume.game.getString("gameId"))
        assertEquals(listOf("GET /api/hosted/state", "GET /api/hosted/state"), allCalls)
    }
}
