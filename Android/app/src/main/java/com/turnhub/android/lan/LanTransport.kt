package com.turnhub.android.lan

import org.json.JSONObject
import org.json.JSONTokener
import java.io.DataInputStream
import java.io.DataOutputStream
import java.net.InetAddress
import java.net.InetSocketAddress
import java.net.ServerSocket
import java.net.Socket
import java.security.MessageDigest
import java.security.cert.X509Certificate
import java.nio.ByteBuffer
import java.nio.charset.CodingErrorAction
import java.util.concurrent.ArrayBlockingQueue
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.ThreadPoolExecutor
import java.util.concurrent.TimeUnit
import javax.net.ssl.SSLContext
import javax.net.ssl.SSLSocket
import javax.net.ssl.TrustManager
import javax.net.ssl.X509TrustManager

/** One bounded UTF-8 JSON request/reply per pinned TLS connection; no HTTP server. */
internal object LanFrames {
    fun read(socket: Socket): JSONObject {
        val input = DataInputStream(socket.getInputStream())
        val size = input.readInt()
        require(size in 1..LAN_MAX_FRAME)
        val bytes = ByteArray(size)
        input.readFully(bytes)
        val text = Charsets.UTF_8.newDecoder().onMalformedInput(CodingErrorAction.REPORT)
            .onUnmappableCharacter(CodingErrorAction.REPORT).decode(ByteBuffer.wrap(bytes)).toString()
        // Android's built-in JSON parser is recursive. Bound nesting before
        // parsing, including unauthenticated frames, without counting quoted braces.
        validateLanJson(text)
        val parser = JSONTokener(text)
        val value = parser.nextValue() as? JSONObject ?: error("Expected an object")
        require(parser.nextClean() == '\u0000')
        return value
    }
    fun write(socket: Socket, value: JSONObject) {
        val bytes = value.toString().toByteArray(Charsets.UTF_8)
        require(bytes.size in 1..LAN_MAX_FRAME)
        DataOutputStream(socket.getOutputStream()).apply { writeInt(bytes.size); write(bytes); flush() }
    }
}

/** Reject lenient JSON extensions before the recursive platform parser runs. */
private val lanJsonNumber = Regex("-?(0|[1-9][0-9]*)(\\.[0-9]+)?([eE][+-]?[0-9]+)?")
private fun validateLanJson(text: String) {
    var depth = 0
    var index = 0
    val separators = "{}[],: \t\r\n"
    while (index < text.length) {
        when (val c = text[index]) {
            '{', '[' -> { depth++; require(depth <= 32); index++ }
            '}', ']' -> { depth--; require(depth >= 0); index++ }
            ':', ',', ' ', '\t', '\r', '\n' -> index++
            '"' -> {
                index++
                var closed = false
                while (index < text.length) {
                    val next = text[index++]
                    if (next == '"') { closed = true; break }
                    if (next == '\\') { require(index < text.length); index++ }
                }
                require(closed)
            }
            else -> {
                val start = index
                while (index < text.length && text[index] !in separators) index++
                val token = text.substring(start, index)
                require(token == "true" || token == "false" || token == "null" || token.matches(lanJsonNumber))
            }
        }
    }
    require(depth == 0)
}

internal class LanServer(private val socket: ServerSocket, private val handle: (JSONObject) -> JSONObject) : AutoCloseable {
    private val connections = ConcurrentHashMap.newKeySet<Socket>()
    private val workers = ThreadPoolExecutor(4, 4, 0, TimeUnit.SECONDS, ArrayBlockingQueue<Runnable>(4))
    private val acceptor = Thread({
        while (!socket.isClosed) {
            val client = try { socket.accept() } catch (_: Exception) { break }
            client.soTimeout = 4_000
            connections += client
            try {
                workers.execute {
                    try {
                        client.use {
                            (it as? SSLSocket)?.apply { enabledProtocols = supportedProtocols.filter { p -> p == "TLSv1.3" || p == "TLSv1.2" }.toTypedArray(); startHandshake() }
                            LanFrames.write(it, handle(LanFrames.read(it)))
                        }
                    } catch (_: Exception) { runCatching { client.close() } }
                    finally { connections -= client }
                }
            } catch (_: Exception) { connections -= client; runCatching { client.close() } }
        }
    }, "TurnHub LAN listener").apply { isDaemon = true; start() }

    val port get() = socket.localPort
    override fun close() {
        runCatching { socket.close() }
        connections.forEach { runCatching { it.close() } }
        workers.shutdownNow()
        acceptor.interrupt()
    }
}

internal fun lanExchange(invite: LanInvite, request: JSONObject, bind: (Socket) -> Unit = {}): JSONObject {
    val trust = object : X509TrustManager {
        override fun getAcceptedIssuers(): Array<X509Certificate> = emptyArray()
        override fun checkClientTrusted(chain: Array<X509Certificate>, authType: String) { error("Not a client-certificate authority") }
        override fun checkServerTrusted(chain: Array<X509Certificate>, authType: String) {
            require(chain.isNotEmpty() && sameSecret(MessageDigest.getInstance("SHA-256").digest(chain[0].encoded).hex(), invite.pin))
            chain[0].checkValidity()
        }
    }
    val context = SSLContext.getInstance("TLS").apply { init(null, arrayOf<TrustManager>(trust), null) }
    (context.socketFactory.createSocket() as SSLSocket).use { socket ->
        socket.soTimeout = 4_000
        bind(socket)
        socket.connect(InetSocketAddress(InetAddress.getByName(invite.address), invite.port), 3_000)
        socket.enabledProtocols = socket.supportedProtocols.filter { it == "TLSv1.3" || it == "TLSv1.2" }.toTypedArray()
        socket.startHandshake()
        LanFrames.write(socket, request)
        val reply = LanFrames.read(socket)
        require(reply.integer("v") == LAN_VERSION.toLong() && reply.text("session") == invite.session)
        return reply
    }
}
