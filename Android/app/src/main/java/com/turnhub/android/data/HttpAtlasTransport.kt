package com.turnhub.android.data

import com.turnhub.android.protocol.AccessibilitySettings
import com.turnhub.android.protocol.AtlasInfo
import com.turnhub.android.protocol.ControlResult
import com.turnhub.android.protocol.GameSettingsInfo
import com.turnhub.android.protocol.LedStyle
import com.turnhub.android.protocol.LoginResult
import com.turnhub.android.protocol.ProfileSummary
import com.turnhub.android.protocol.SeatEntry
import com.turnhub.android.protocol.SessionInfo
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
import java.net.URLEncoder
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
 * Also the [AtlasSessionTransport]: all requests go through [request], which
 * handles form bodies and the `X-TurnHub-Token` session header.
 */
class HttpAtlasTransport(
    private val endpoint: AtlasEndpoint,
    private val opener: HttpConnectionOpener = HttpConnectionOpener.Default,
    private val ioDispatcher: CoroutineDispatcher = Dispatchers.IO,
    private val connectTimeoutMs: Int = 3_000,
    private val readTimeoutMs: Int = 3_000,
) : AtlasTransport, AtlasSessionTransport {

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

    override suspend fun getSeats(): List<SeatEntry> {
        val response = request("GET", "/api/seats")
        if (response.code != HttpURLConnection.HTTP_OK) {
            throw AtlasException(AtlasFailure.HttpStatus(response.code, "Atlas seats request failed"))
        }
        return parse { AtlasWireParser.parseSeats(response.body) }
    }

    // --- authenticated profile/session routes -------------------------------

    override suspend fun getProfiles(): List<ProfileSummary> {
        val response = request("GET", "/api/profiles")
        requireOk(response, "Atlas profiles request failed")
        return parse { AtlasWireParser.parseProfiles(response.body) }
    }

    override suspend fun login(profileId: String, pin: String): LoginResult {
        val response = request("POST", "/api/session/login", formBody = form("profileId" to profileId, "pin" to pin))
        if (response.code == HttpURLConnection.HTTP_UNAUTHORIZED) {
            // Here 401 means the profile/PIN was refused, not an expired session.
            throw AtlasException(
                AtlasFailure.Rejected(AtlasWireParser.errorMessage(response.body) ?: "Profile or PIN was not accepted"),
            )
        }
        requireOk(response, "Sign-in failed")
        return parse { AtlasWireParser.parseLogin(response.body) }
    }

    override suspend fun me(token: String): SessionInfo {
        val response = request("GET", "/api/session/me", headers = auth(token))
        requireOk(response, "Could not read your session")
        return parse { AtlasWireParser.parseSessionMe(response.body) }
    }

    override suspend fun join(token: String): String? {
        val response = request("POST", "/api/session/join", headers = auth(token), formBody = "")
        requireOk(response, "Could not join the table")
        return parse { AtlasWireParser.parseControlResult(response.body).message }
    }

    override suspend fun control(
        token: String,
        action: ControlAction,
        expectedRevision: Long?,
        expectedBootId: String?,
    ): ControlResult {
        val fields = buildList {
            if (expectedRevision != null && expectedBootId != null) {
                add("expectedRevision" to expectedRevision.toString())
                add("expectedBootId" to expectedBootId)
            }
        }
        val response = request("POST", action.path, headers = auth(token), formBody = form(*fields.toTypedArray()))
        if (response.code == HttpURLConnection.HTTP_CONFLICT) {
            // REJECTED / CONFLICT: a normal outcome carrying Atlas's reason.
            return parse { AtlasWireParser.parseControlResult(response.body) }
        }
        requireOk(response, "Atlas did not accept the request")
        return parse { AtlasWireParser.parseControlResult(response.body) }
    }

    override suspend fun getGameSettings(token: String): GameSettingsInfo {
        val response = request("GET", "/api/game/settings", headers = auth(token))
        requireOk(response, "Could not read the game settings")
        return parse { AtlasWireParser.parseGameSettings(response.body) }
    }

    override suspend fun setTurnTimer(token: String, turnTimerMs: Long): String? {
        val response = request(
            "POST",
            "/api/game/settings",
            headers = auth(token),
            formBody = form("turnTimerMs" to turnTimerMs.toString()),
        )
        requireOk(response, "Atlas did not save the turn timer")
        return parse { AtlasWireParser.parseControlResult(response.body).message }
    }

    override suspend fun getAccessibility(token: String): AccessibilitySettings {
        val response = request("GET", "/api/session/accessibility", headers = auth(token))
        requireOk(response, "Could not read your Sigil accessibility settings")
        return parse { AtlasWireParser.parseAccessibility(response.body) }
    }

    override suspend fun saveAccessibility(
        token: String,
        sigilSound: Boolean,
        ledStyle: LedStyle,
        longPressMs: Int,
        winHoldMs: Int,
    ): AccessibilitySettings {
        val response = request(
            "POST",
            "/api/session/accessibility",
            headers = auth(token),
            formBody = form(
                "sigilSound" to if (sigilSound) "1" else "0",
                "ledStyle" to ledStyle.wire,
                "longPressMs" to longPressMs.toString(),
                "winHoldMs" to winHoldMs.toString(),
            ),
        )
        requireOk(response, "Atlas did not save your Sigil accessibility settings")
        return parse { AtlasWireParser.parseAccessibility(response.body) }
    }

    override suspend fun logout(token: String) {
        val response = request("POST", "/api/session/logout", headers = auth(token), formBody = "")
        if (response.code != HttpURLConnection.HTTP_UNAUTHORIZED) requireOk(response, "Sign-out failed")
    }

    private fun auth(token: String) = mapOf(TOKEN_HEADER to token)

    private fun form(vararg fields: Pair<String, String>): String =
        fields.joinToString("&") { (name, value) ->
            URLEncoder.encode(name, "UTF-8") + "=" + URLEncoder.encode(value, "UTF-8")
        }

    /** Maps Atlas's error statuses to failures, using Atlas's own `error` text when present. */
    private fun requireOk(response: Response, fallback: String) {
        if (response.code in 200..299) return
        val reason = AtlasWireParser.errorMessage(response.body)
        throw AtlasException(
            when (response.code) {
                HttpURLConnection.HTTP_UNAUTHORIZED -> AtlasFailure.SessionExpired
                in 400..499 -> AtlasFailure.Rejected(reason ?: "$fallback (HTTP ${response.code})")
                else -> AtlasFailure.HttpStatus(response.code, reason ?: fallback)
            },
        )
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

        /** Session token header (protocol/http-v1.md). */
        const val TOKEN_HEADER = "X-TurnHub-Token"
    }
}
