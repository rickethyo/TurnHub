package com.turnhub.android.data

import com.turnhub.android.protocol.AccessibilitySettings
import com.turnhub.android.protocol.AtlasInfo
import com.turnhub.android.protocol.ControlResult
import com.turnhub.android.protocol.GameSettingsInfo
import com.turnhub.android.protocol.LedStyle
import com.turnhub.android.protocol.LoginResult
import com.turnhub.android.protocol.ProfileSummary
import com.turnhub.android.protocol.AvatarIcon
import com.turnhub.android.protocol.SeatEntry
import com.turnhub.android.protocol.SessionInfo
import com.turnhub.android.protocol.SetupStatus
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

    override suspend fun getAvatars(): List<AvatarIcon> {
        val response = request("GET", "/api/avatars")
        requireOk(response, "Could not read the avatars")
        return parse { AtlasWireParser.parseAvatars(response.body) }
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
        lifeApprovalMs: Int,
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
                "lifeApprovalMs" to lifeApprovalMs.toString(),
            ),
        )
        requireOk(response, "Atlas did not save your Sigil accessibility settings")
        return parse { AtlasWireParser.parseAccessibility(response.body) }
    }

    override suspend fun logout(token: String) {
        val response = request("POST", "/api/session/logout", headers = auth(token), formBody = "")
        if (response.code != HttpURLConnection.HTTP_UNAUTHORIZED) requireOk(response, "Sign-out failed")
    }

    override suspend fun postControl(
        token: String,
        path: String,
        fields: List<Pair<String, String>>,
    ): ControlResult {
        val response = request("POST", path, headers = auth(token), formBody = form(*fields.toTypedArray()))
        if (response.code == HttpURLConnection.HTTP_UNAUTHORIZED) throw AtlasException(AtlasFailure.SessionExpired)
        if (response.code in 400..499) {
            val reason = AtlasWireParser.errorMessage(response.body)
            return try {
                AtlasWireParser.parseControlResult(response.body).let { it.copy(message = it.message ?: reason) }
            } catch (_: AtlasWireException) {
                ControlResult(ok = false, status = "REJECTED", message = reason, revision = null, bootId = null)
            }
        }
        requireOk(response, "Atlas did not accept the request")
        return try {
            parse { AtlasWireParser.parseControlResult(response.body) }
        } catch (_: AtlasException) {
            ControlResult(ok = true, status = "ACCEPTED", message = null, revision = null, bootId = null)
        }
    }

    override suspend fun saveGameSettings(
        token: String,
        gameProfile: String?,
        startingLife: Int?,
        turnTimerMs: Long?,
    ): String? {
        val fields = buildList {
            gameProfile?.let { add("gameProfile" to it) }
            startingLife?.let { add("startingLife" to it.toString()) }
            turnTimerMs?.let { add("turnTimerMs" to it.toString()) }
        }
        val response = request("POST", "/api/game/settings", headers = auth(token), formBody = form(*fields.toTypedArray()))
        requireOk(response, "Atlas did not save the game settings")
        return messageOf(response.body)
    }

    override suspend fun saveProfile(token: String, name: String?, pin: String?): String? {
        val fields = buildList {
            name?.let { add("name" to it) }
            pin?.let { add("pin" to it) }
        }
        val response = request("POST", "/api/session/profile", headers = auth(token), formBody = form(*fields.toTypedArray()))
        requireOk(response, "Atlas did not save your profile")
        return messageOf(response.body)
    }

    override suspend fun getPersonalization(token: String): Personalization {
        val response = request("GET", "/api/session/personalization", headers = auth(token))
        requireOk(response, "Could not read your personalization")
        return parsePersonalization(response.body)
    }

    override suspend fun savePersonalization(token: String, color: String?, avatar: Int?): Personalization {
        val fields = buildList {
            color?.let { add("color" to it) }
            avatar?.let { add("avatar" to it.toString()) }
        }
        val response = request(
            "POST",
            "/api/session/personalization",
            headers = auth(token),
            formBody = form(*fields.toTypedArray()),
        )
        requireOk(response, "Atlas did not save your personalization")
        return parsePersonalization(response.body)
    }

    private fun parsePersonalization(body: String): Personalization = try {
        val root = org.json.JSONObject(body)
        Personalization(
            color = if (root.isNull("color")) null else root.optString("color").takeIf { it.startsWith("#") },
            avatar = root.optInt("avatar", 0),
            cardPresent = root.optBoolean("card", true),
        )
    } catch (e: org.json.JSONException) {
        throw AtlasException(AtlasFailure.Malformed(e.message ?: "personalization"))
    }

    private fun messageOf(body: String): String? = try {
        org.json.JSONObject(body).optString("message").takeIf { it.isNotBlank() }
    } catch (_: org.json.JSONException) {
        null
    }

    override suspend fun raw(
        method: String,
        path: String,
        token: String,
        fields: List<Pair<String, String>>,
    ): RawResponse {
        val response = request(
            method,
            path,
            headers = auth(token),
            formBody = if (method == "POST") form(*fields.toTypedArray()) else null,
        )
        return RawResponse(response.code, response.body)
    }

    override suspend fun register(name: String, pin: String): LoginResult {
        val response = request("POST", "/api/profiles/register", formBody = form("name" to name, "pin" to pin))
        requireOk(response, "Could not create the account")
        return parse { AtlasWireParser.parseLogin(response.body) }
    }

    override suspend fun reportLatestFirmware(fields: List<Pair<String, String>>): Int {
        val response = request("POST", "/api/updates/latest", formBody = form(*fields.toTypedArray()))
        requireOk(response, "Atlas did not take the firmware report")
        return try {
            org.json.JSONObject(response.body).optInt("updatesAvailable", 0)
        } catch (_: org.json.JSONException) {
            0
        }
    }

    override suspend fun getSetup(): SetupStatus {
        val response = request("GET", "/api/setup")
        requireOk(response, "Could not read the setup status")
        return parse { SetupStatus.parse(response.body) }
    }

    override suspend fun upload(path: String, token: String, field: String, fileName: String, bytes: ByteArray): RawResponse {
        val boundary = "TurnHubPackage" + System.nanoTime().toString(16)
        val head = "--$boundary\r\nContent-Disposition: form-data; name=\"$field\"; filename=\"$fileName\"\r\n" +
            "Content-Type: application/octet-stream\r\n\r\n"
        val tail = "\r\n--$boundary--\r\n"
        val body = head.toByteArray(Charsets.UTF_8) + bytes + tail.toByteArray(Charsets.UTF_8)
        val response = request(
            "POST",
            path,
            headers = auth(token),
            rawBody = body,
            contentType = "multipart/form-data; boundary=$boundary",
            // Atlas writes flash while it receives; allow for a slow AP.
            readTimeout = UPLOAD_TIMEOUT_MS,
        )
        return RawResponse(response.code, response.body)
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
        rawBody: ByteArray? = null,
        contentType: String? = null,
        readTimeout: Int = readTimeoutMs,
    ): Response = withContext(ioDispatcher) {
        val connection = try {
            opener.open(URL(endpoint.baseUrl + path))
        } catch (e: IOException) {
            throw classify(e)
        }
        try {
            connection.requestMethod = method
            connection.connectTimeout = connectTimeoutMs
            connection.readTimeout = readTimeout
            connection.useCaches = false
            // A captive portal or other device redirecting us is not an Atlas.
            connection.instanceFollowRedirects = false
            connection.setRequestProperty("Accept", "application/json")
            headers.forEach { (name, value) -> connection.setRequestProperty(name, value) }
            if (formBody != null) {
                connection.doOutput = true
                connection.setRequestProperty("Content-Type", "application/x-www-form-urlencoded")
                connection.outputStream.use { it.write(formBody.toByteArray(Charsets.UTF_8)) }
            } else if (rawBody != null) {
                connection.doOutput = true
                connection.setFixedLengthStreamingMode(rawBody.size)
                connection.setRequestProperty("Content-Type", contentType ?: "application/octet-stream")
                connection.outputStream.use { it.write(rawBody) }
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

        /** A firmware upload: about 1.3 MB over the Atlas AP while Atlas writes flash. */
        const val UPLOAD_TIMEOUT_MS = 180_000
    }
}
