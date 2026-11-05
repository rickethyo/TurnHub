package com.turnhub.android.data

import com.sun.net.httpserver.HttpServer
import com.turnhub.android.testing.Fixtures
import kotlinx.coroutines.runBlocking
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertThrows
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import java.net.HttpURLConnection
import java.net.InetSocketAddress
import java.net.ServerSocket
import java.net.URL

/** Exercises the real [HttpURLConnection] path against the JDK's local HTTP server. */
class HttpAtlasTransportTest {

    private lateinit var server: HttpServer
    private val routes = mutableMapOf<String, Triple<Int, String, Long>>()

    /** "METHOD path token body" for each request the server received. */
    private val received = mutableListOf<String>()
    private val queries = mutableListOf<String>()

    @Before
    fun start() {
        server = HttpServer.create(InetSocketAddress("127.0.0.1", 0), 0)
        server.createContext("/") { exchange ->
            queries += exchange.requestURI.rawQuery.orEmpty()
            val requestBody = exchange.requestBody.use { it.readBytes().toString(Charsets.UTF_8) }
            received += listOf(
                exchange.requestMethod,
                exchange.requestURI.path,
                exchange.requestHeaders.getFirst("X-TurnHub-Token").orEmpty(),
                requestBody,
            ).joinToString(" ").trim()
            val (code, body, delayMs) = routes[exchange.requestURI.path] ?: Triple(404, "Not found", 0L)
            if (delayMs > 0) Thread.sleep(delayMs)
            val bytes = body.toByteArray()
            if (exchange.requestURI.path == "/api/avatar") exchange.responseHeaders.add("Content-Type", "image/jpeg")
            if (code in 300..399) exchange.responseHeaders.add("Location", "http://example.invalid/")
            exchange.sendResponseHeaders(code, if (bytes.isEmpty()) -1 else bytes.size.toLong())
            exchange.responseBody.use { it.write(bytes) }
        }
        server.start()
    }

    @After
    fun stop() = server.stop(0)

    @Test fun `raw GET includes encoded receipt lookup fields`() = runBlocking {
        routes["GET /api/action-receipt"] = Triple(200, "{}", 0L)
        transport().raw("GET", "/api/action-receipt", "token", listOf("actionId" to "a b&c"))
        assertEquals("actionId=a+b%26c", queries.last())
    }

    private fun transport(port: Int = server.address.port, readTimeoutMs: Int = 2_000) = HttpAtlasTransport(
        endpoint = AtlasEndpoint.parse("https://127.0.0.1:$port").getOrThrow(),
        baseOpener = HttpConnectionOpener { url ->
            URL(url.toString().replaceFirst("https://", "http://")).openConnection() as HttpURLConnection
        },
        readTimeoutMs = readTimeoutMs,
    )

    private fun failureOf(block: suspend () -> Unit): AtlasFailure =
        assertThrows(AtlasException::class.java) { runBlocking { block() } }.failure

    @Test
    fun `SD artwork preview and approval outlast the polling deadline`() = runBlocking {
        routes["/api/avatar"] = Triple(200, "test-image", 400)
        routes["/api/avatar/review"] = Triple(200, "{\"ok\":true}", 400)
        routes["/api/session/avatar"] = Triple(200, "{\"ok\":true}", 400)
        val client = transport(readTimeoutMs = 100)
        assertEquals("test-image", client.getAvatarImage(
            "/api/avatar?profileId=ABC12345&revision=abcdef01&pending=1", "admin-token").toString(Charsets.UTF_8))
        assertTrue(client.raw("POST", "/api/avatar/review", "admin-token",
            listOf("action" to "approve")).ok)
        assertTrue(client.raw("POST", "/api/session/avatar", "player-token",
            listOf("action" to "submit")).ok)
    }

    @Test
    fun `SD artwork still rejects responses above 48 KiB`() {
        routes["/api/avatar"] = Triple(200, "x".repeat(48 * 1024 + 1), 0)
        assertTrue(failureOf { transport().getAvatarImage(
            "/api/avatar?profileId=ABC12345&revision=abcdef01", "") } is AtlasFailure.Malformed)
    }

    @Test
    fun `reads info and state from a live server`() = runBlocking {
        routes["/api/v1/info"] = Triple(200, Fixtures.text("info.response.json"), 0)
        routes["/api/v1/state"] = Triple(200, Fixtures.text("state-hosted.response.json"), 0)

        val info = transport().getInfo()
        val state = transport().getState()

        assertEquals("THA-025448410001", info.atlasId)
        assertEquals(7L, state.revision)
        assertEquals(4, state.hub?.players)
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
    fun `reads and saves Sigil accessibility for the session`() = runBlocking {
        routes["/api/session/accessibility"] = Triple(200, Fixtures.text("accessibility.response.json"), 0)
        assertEquals(3000, transport().getAccessibility("secret").longPressMs)

        transport().saveAccessibility("secret", true, com.turnhub.android.protocol.LedStyle.MONOCHROME_SAFE, 2500, 7000, 30000)

        assertEquals(
            listOf(
                "GET /api/session/accessibility secret",
                "POST /api/session/accessibility secret sigilSound=1&ledStyle=monochrome-safe&longPressMs=2500&winHoldMs=7000&lifeApprovalMs=30000",
            ),
            received,
        )
    }

    @Test
    fun `a stalled Atlas times out`() {
        routes["/api/v1/state"] = Triple(200, Fixtures.text("state-hosted.response.json"), 1_500)
        val failure = failureOf { transport(readTimeoutMs = 200).getState() }
        assertTrue(failure is AtlasFailure.Timeout)
        assertTrue(failure.technicalDetail!!.startsWith("SocketTimeoutException"))
    }

    @Test
    fun `setup TLS refuses a connection opener that downgrades to HTTP`() {
        routes["/api/v1/info"] = Triple(200, Fixtures.text("info.response.json"), 0)
        val client = HttpAtlasTransport(
            endpoint = AtlasEndpoint.DEFAULT,
            baseOpener = HttpConnectionOpener {
                URL("http://127.0.0.1:${server.address.port}/api/v1/info")
                    .openConnection() as HttpURLConnection
            },
        )

        assertTrue(failureOf { client.getInfo() } is AtlasFailure.TlsIdentity)
    }

    @Test
    fun `setup TLS blocks ordinary API routes before opening a connection`() {
        var opened = false
        val client = HttpAtlasTransport(
            endpoint = AtlasEndpoint.DEFAULT,
            baseOpener = HttpConnectionOpener {
                opened = true
                throw java.io.IOException("Should not be opened")
            },
        )

        assertTrue(failureOf { client.raw("GET", "/api/profiles", "", emptyList()) } is AtlasFailure.InvalidEndpoint)
        assertTrue(failureOf { client.getState() } is AtlasFailure.InvalidEndpoint)
        assertFalse(opened)
    }
}
