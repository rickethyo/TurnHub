package com.turnhub.android.data

import com.turnhub.android.protocol.AtlasInfo
import com.turnhub.android.protocol.AtlasWireException
import com.turnhub.android.protocol.AtlasWireParser
import com.turnhub.android.protocol.StateSnapshot
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.io.ByteArrayOutputStream
import java.io.IOException
import java.io.InputStream
import java.net.HttpURLConnection
import java.net.SocketTimeoutException
import java.net.URL
import java.net.UnknownServiceException

/** Opens the connection for a URL; lets Android pick which network carries it. */
fun interface HttpConnectionOpener {
    fun open(url: URL): HttpURLConnection

    companion object {
        /** The JVM/platform default route. */
        val Default = HttpConnectionOpener { url -> url.openConnection() as HttpURLConnection }
    }
}

/**
 * [AtlasTransport] over Atlas's existing local HTTP API (protocol/http-v1.md),
 * using the platform [HttpURLConnection] -- no HTTP client dependency.
 *
 * All requests go through [request], which already supports the form bodies
 * and `X-TurnHub-Token` header the next milestone's session calls need.
 */
class HttpAtlasTransport(
    private val endpoint: AtlasEndpoint,
    private val opener: HttpConnectionOpener = HttpConnectionOpener.Default,
    private val ioDispatcher: CoroutineDispatcher = Dispatchers.IO,
    private val connectTimeoutMs: Int = 3_000,
    private val readTimeoutMs: Int = 3_000,
) : AtlasTransport {

    override suspend fun getInfo(): AtlasInfo {
        val response = request("GET", "/api/v1/info")
        when (response.code) {
            HttpURLConnection.HTTP_OK -> Unit
            in 300..499 -> throw AtlasException(
                AtlasFailure.NotTurnHub("No TurnHub API at ${endpoint.baseUrl} (HTTP ${response.code})"),
            )
            else -> throw AtlasException(AtlasFailure.HttpStatus(response.code, "Atlas info request failed"))
        }
        return parse { AtlasWireParser.parseInfo(response.body) }
    }

    override suspend fun getState(): StateSnapshot {
        val response = request("GET", "/api/v1/state")
        if (response.code != HttpURLConnection.HTTP_OK) {
            val detail = if (response.code == HttpURLConnection.HTTP_UNAVAILABLE) {
                "Atlas has not published table state yet"
            } else {
                "Atlas state request failed"
            }
            throw AtlasException(AtlasFailure.HttpStatus(response.code, detail))
        }
        return parse { AtlasWireParser.parseState(response.body) }
    }

    private inline fun <T> parse(block: () -> T): T = try {
        block()
    } catch (e: AtlasWireException.NotTurnHub) {
        throw AtlasException(AtlasFailure.NotTurnHub(e.message ?: "Not a TurnHub response"))
    } catch (e: AtlasWireException.Malformed) {
        throw AtlasException(AtlasFailure.Malformed(e.message ?: "malformed body"))
    }

    private class Response(val code: Int, val body: String)

    private suspend fun request(
        method: String,
        path: String,
        headers: Map<String, String> = emptyMap(),
        formBody: String? = null,
    ): Response = withContext(ioDispatcher) {
        val connection = try {
            opener.open(URL(endpoint.baseUrl + path))
        } catch (e: IOException) {
            throw classify(e)
        }
        try {
            connection.requestMethod = method
            connection.connectTimeout = connectTimeoutMs
            connection.readTimeout = readTimeoutMs
            connection.useCaches = false
            // A captive portal or other device redirecting us is not an Atlas.
            connection.instanceFollowRedirects = false
            connection.setRequestProperty("Accept", "application/json")
            headers.forEach { (name, value) -> connection.setRequestProperty(name, value) }
            if (formBody != null) {
                connection.doOutput = true
                connection.setRequestProperty("Content-Type", "application/x-www-form-urlencoded")
                connection.outputStream.use { it.write(formBody.toByteArray(Charsets.UTF_8)) }
            }
            val code = connection.responseCode
            val stream = if (code in 200..299) connection.inputStream else connection.errorStream
            Response(code, stream?.use { readLimited(it) } ?: "")
        } catch (e: IOException) {
            throw classify(e)
        } finally {
            connection.disconnect()
        }
    }

    private fun readLimited(stream: InputStream): String {
        val out = ByteArrayOutputStream()
        val buffer = ByteArray(8 * 1024)
        while (true) {
            val read = stream.read(buffer)
            if (read < 0) break
            out.write(buffer, 0, read)
            if (out.size() > MAX_BODY_BYTES) {
                throw AtlasException(AtlasFailure.Malformed("response larger than ${MAX_BODY_BYTES / 1024} KiB"))
            }
        }
        return out.toString(Charsets.UTF_8.name())
    }

    private fun classify(e: IOException): AtlasException {
        // e.g. "SocketTimeoutException: failed to connect to /192.168.4.1 (port 80) from /192.168.4.2 ..."
        val detail = "${e.javaClass.simpleName}: ${e.message}"
        return AtlasException(
            when {
                // Also what Android 17 local-network blocking looks like for TCP.
                e is SocketTimeoutException -> AtlasFailure.Timeout(detail)
                e is UnknownServiceException && e.message.orEmpty().contains("CLEARTEXT", ignoreCase = true) ->
                    AtlasFailure.CleartextBlocked(endpoint.host)
                // Refused, no route, unknown host, reset, ...
                else -> AtlasFailure.Unreachable(detail)
            },
        )
    }

    private companion object {
        /** A full 16-player snapshot is a few KiB; anything huge is not Atlas. */
        const val MAX_BODY_BYTES = 256 * 1024
    }
}
