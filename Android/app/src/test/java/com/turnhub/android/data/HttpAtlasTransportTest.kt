package com.turnhub.android.data

import com.sun.net.httpserver.HttpServer
import com.turnhub.android.testing.Fixtures
import kotlinx.coroutines.runBlocking
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertSame
import org.junit.Assert.assertThrows
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import java.net.InetSocketAddress
import java.net.ServerSocket

/** Exercises the real [HttpURLConnection] path against the JDK's local HTTP server. */
class HttpAtlasTransportTest {

    private lateinit var server: HttpServer
    private val routes = mutableMapOf<String, Triple<Int, String, Long>>()

    @Before
    fun start() {
        server = HttpServer.create(InetSocketAddress("127.0.0.1", 0), 0)
        server.createContext("/") { exchange ->
            val (code, body, delayMs) = routes[exchange.requestURI.path] ?: Triple(404, "Not found", 0L)
            if (delayMs > 0) Thread.sleep(delayMs)
            val bytes = body.toByteArray()
            if (code in 300..399) exchange.responseHeaders.add("Location", "http://example.invalid/")
            exchange.sendResponseHeaders(code, if (bytes.isEmpty()) -1 else bytes.size.toLong())
            exchange.responseBody.use { it.write(bytes) }
        }
        server.start()
    }

    @After
    fun stop() = server.stop(0)

    private fun transport(port: Int = server.address.port, readTimeoutMs: Int = 2_000) = HttpAtlasTransport(
        endpoint = AtlasEndpoint.parse("http://127.0.0.1:$port").getOrThrow(),
        readTimeoutMs = readTimeoutMs,
    )

    private fun failureOf(block: suspend () -> Unit): AtlasFailure =
        assertThrows(AtlasException::class.java) { runBlocking { block() } }.failure

    @Test
    fun `reads info and state from a live server`() = runBlocking {
        routes["/api/v1/info"] = Triple(200, Fixtures.text("info.response.json"), 0)
        routes["/api/v1/state"] = Triple(200, Fixtures.text("commander.response.json"), 0)

        val info = transport().getInfo()
        val state = transport().getState()

        assertEquals("THA-025448410001", info.atlasId)
        assertEquals(13L, state.revision)
        assertEquals(2, state.players.size)
    }

    @Test
    fun `a server without the TurnHub API is not an Atlas`() {
        assertTrue(failureOf { transport().getInfo() } is AtlasFailure.NotTurnHub)
    }

    @Test
    fun `an HTML page is not an Atlas`() {
        routes["/api/v1/info"] = Triple(200, "<html><body>Welcome</body></html>", 0)
        assertTrue(failureOf { transport().getInfo() } is AtlasFailure.NotTurnHub)
    }

    @Test
    fun `redirects are not followed`() {
        routes["/api/v1/info"] = Triple(302, "", 0)
        assertTrue(failureOf { transport().getInfo() } is AtlasFailure.NotTurnHub)
    }

    @Test
    fun `state not yet published is an HTTP failure`() {
        routes["/api/v1/state"] = Triple(503, """{"error":"State snapshot is not configured"}""", 0)
        val failure = failureOf { transport().getState() }
        assertTrue(failure is AtlasFailure.HttpStatus)
        assertEquals(503, (failure as AtlasFailure.HttpStatus).code)
    }

    @Test
    fun `malformed state is reported as malformed`() {
        routes["/api/v1/state"] = Triple(200, """{"protocolVersion":"0.1"}""", 0)
        assertTrue(failureOf { transport().getState() } is AtlasFailure.Malformed)
    }

    @Test
    fun `connection refused is unreachable`() {
        val closedPort = ServerSocket(0).use { it.localPort }
        val failure = failureOf { transport(port = closedPort).getInfo() }
        assertTrue(failure is AtlasFailure.Unreachable)
        assertTrue(failure.technicalDetail!!.startsWith("ConnectException"))
    }

    @Test
    fun `a stalled Atlas times out`() {
        routes["/api/v1/state"] = Triple(200, Fixtures.text("running.response.json"), 1_500)
        val failure = failureOf { transport(readTimeoutMs = 200).getState() }
        assertTrue(failure is AtlasFailure.Timeout)
        assertTrue(failure.technicalDetail!!.startsWith("SocketTimeoutException"))
    }
}
