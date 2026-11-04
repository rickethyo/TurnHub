package com.turnhub.android.protocol

import org.json.JSONArray
import org.json.JSONException
import org.json.JSONObject

/** A response body that could not be read as the expected TurnHub wire shape. */
sealed class AtlasWireException(message: String) : Exception(message) {
    /** The responder is not a TurnHub Atlas at all (wrong product/device, or not JSON). */
    class NotTurnHub(message: String) : AtlasWireException(message)

    /** It claims to be TurnHub, but the body breaks the documented schema. */
    class Malformed(message: String) : AtlasWireException(message)
}

/**
 * Parses `/api/v1/info` and `/api/v1/state` bodies into the DTOs in this
 * package, using the platform `org.json` parser (no extra dependency).
 *
 * Required fields, types and unsigned 32-bit ranges are enforced; unknown
 * fields are ignored so Atlas can add optional fields compatibly
 * (protocol/README.md, versioning rule 3). Closed enums fail closed: a value
 * this app doesn't know is reported as [AtlasWireException.Malformed] rather
 * than guessed at.
 */
object AtlasWireParser {

    private const val UINT32_MAX = 4_294_967_295L
    private val BOOT_ID = Regex("^[0-9A-F]{32}$")

    fun parseInfo(body: String): AtlasInfo {
        val root = parseObject(body) { AtlasWireException.NotTurnHub("Response is not TurnHub JSON") }
        val product = root.optString("product")
        val deviceType = root.optString("deviceType")
        if (product != "TurnHub" || deviceType != "Atlas") {
            throw AtlasWireException.NotTurnHub("Responder is not a TurnHub Atlas")
        }
        return wrap {
            val capabilities = root.obj("capabilities")
            AtlasInfo(
                product = product,
                deviceType = deviceType,
                atlasId = root.nonEmptyString("atlasId"),
                firmwareVersion = root.string("firmwareVersion"),
                apiVersion = root.string("apiVersion"),
                protocolVersion = root.string("protocolVersion"),
                radioProtocolVersion = root.int("radioProtocolVersion"),
                bootId = root.bootId("bootId"),
                revision = root.uint32("revision"),
                capabilities = AtlasCapabilities(
                    stateSnapshot = capabilities.boolean("stateSnapshot"),
                    sessionControls = capabilities.boolean("sessionControls"),
                    intentEnvelope = capabilities.boolean("intentEnvelope"),
                    events = capabilities.boolean("events"),
                    requestDeduplication = capabilities.boolean("requestDeduplication"),
                ),
                home = if (root.isAbsentOrNull("home")) null else parseHomeAddress(root.obj("home")),
            )
        }
    }

    fun parseState(body: String): StateSnapshot {
        val root = parseObject(body) { AtlasWireException.Malformed("State response is not JSON") }
        return wrap {
            StateSnapshot(
                protocolVersion = root.string("protocolVersion"),
                atlasId = root.nonEmptyString("atlasId"),
                bootId = root.bootId("bootId"),
                revision = root.uint32("revision"),
                hub = if (root.require("hub") == JSONObject.NULL) null else root.obj("hub").let { hub ->
                    val players = hub.int("players")
                    if (players !in 0..255) malformed("Hub players $players is out of range")
                    AtlasHub(
                        name = hub.string("name"),
                        gameId = hub.string("gameId"),
                        state = TableState.fromWire(hub.string("state")) ?: malformed("Unknown hub state"),
                        players = players,
                    )
                },
                battery = if (root.require("battery") == JSONObject.NULL) null else root.obj("battery").let { battery ->
                    val percent = battery.int("percent")
                    if (percent !in 0..100) malformed("Battery percent $percent is out of range")
                    AtlasBattery(
                        percent = percent,
                        low = battery.boolean("low"),
                        charging = battery.boolean("charging"),
                    )
                },
                links = root.obj("links").let { links ->
                    val sigils = links.int("sigils")
                    if (sigils !in 0..16) malformed("Sigils online $sigils is out of range")
                    AtlasLinks(links.boolean("hotspot"), links.boolean("home"), sigils)
                },
            )
        }
    }

    fun parseAvatars(body: String): List<AvatarIcon> {
        val root = parseObject(body) { AtlasWireException.Malformed("Avatars response is not JSON") }
        return wrap {
            val size = root.int("size")
            root.array("avatars").objects().mapNotNull { icon ->
                val rows = icon.array("rows").let { array -> List(array.length()) { array.getString(it) } }
                if (rows.size != size || rows.any { it.length != size }) return@mapNotNull null
                AvatarIcon(id = icon.int("id"), key = icon.string("key"), label = icon.string("label"), rows = rows)
            }
        }
    }

    fun parseProfiles(body: String): List<ProfileSummary> {
        val root = parseObject(body) { AtlasWireException.Malformed("Profiles response is not JSON") }
        return wrap {
            root.array("profiles").objects().map { profile ->
                ProfileSummary(
                    profileId = profile.nonEmptyString("profileId"),
                    name = profile.string("name"),
                    hasPin = profile.boolean("hasPin"),
                    aliases = profile.optJSONArray("aliases")?.let { a -> (0 until a.length()).map { a.getString(it) } }.orEmpty(),
                )
            }
        }
    }

    fun parseLogin(body: String): LoginResult {
        val root = parseObject(body) { AtlasWireException.Malformed("Login response is not JSON") }
        return wrap { LoginResult(token = root.nonEmptyString("token"), profileId = root.string("profileId")) }
    }

    fun parseSessionMe(body: String): SessionInfo {
        val root = parseObject(body) { AtlasWireException.Malformed("Session response is not JSON") }
        return wrap {
            SessionInfo(
                profileId = root.string("profileId"),
                name = root.optionalString("name")?.trim()?.takeIf { it.isNotEmpty() },
                permissions = root.int("permissions"),
            )
        }
    }

    fun parseAccessibility(body: String): AccessibilitySettings {
        val root = parseObject(body) { AtlasWireException.Malformed("Accessibility response is not JSON") }
        return wrap {
            val limits = root.obj("limits")
            val holdLimits = HoldLimits(
                longPressMinMs = limits.int("longPressMinMs"),
                longPressMaxMs = limits.int("longPressMaxMs"),
                winHoldMinMs = limits.int("winHoldMinMs"),
                winHoldMaxMs = limits.int("winHoldMaxMs"),
                minGapMs = limits.int("minGapMs"),
                stepMs = limits.int("stepMs"),
            )
            if (holdLimits.stepMs <= 0) malformed("Hold step must be positive")
            val settings = AccessibilitySettings(
                sigilSound = root.boolean("sigilSound"),
                ledStyle = root.string("ledStyle").let { LedStyle.fromWire(it) ?: malformed("Unknown light style '$it'") },
                longPressMs = root.int("longPressMs"),
                winHoldMs = root.int("winHoldMs"),
                stored = root.boolean("stored"),
                limits = holdLimits,
                lifeApprovalMs = root.int("lifeApprovalMs"),
                lifeApprovalOptionsMs = root.array("lifeApprovalOptionsMs").let { array -> List(array.length()) { array.getInt(it) } },
            )
            if (!holdLimits.allows(settings.longPressMs, settings.winHoldMs)) malformed("Hold times are out of range")
            if (settings.lifeApprovalMs !in settings.lifeApprovalOptionsMs) malformed("Life approval window is not one of the offered choices")
            settings
        }
    }

    fun errorMessage(body: String): String? = try {
        JSONObject(body.trim()).optString("error").takeIf { it.isNotBlank() }
    } catch (_: JSONException) {
        null
    }

    private val PIN_PATTERN = Regex("^[0-9a-f]{64}$")

    private fun parseHomeAddress(home: JSONObject): AtlasHomeAddress {
        val pin = home.string("tlsPin")
        val port = home.int("tlsPort")
        if (!PIN_PATTERN.matches(pin) || port !in 1..65535) throw JSONException("home: invalid tlsPin or tlsPort")
        return AtlasHomeAddress(address = home.nonEmptyString("address"), tlsPort = port, tlsPin = pin)
    }

    // --- strict field helpers -------------------------------------------------

    private inline fun parseObject(body: String, notJson: () -> AtlasWireException): JSONObject =
        try {
            JSONObject(body.trim())
        } catch (_: JSONException) {
            throw notJson()
        }

    /** Converts stray org.json exceptions into [AtlasWireException.Malformed]. */
    private inline fun <T> wrap(block: () -> T): T = try {
        block()
    } catch (e: JSONException) {
        throw AtlasWireException.Malformed(e.message ?: "Malformed JSON")
    }

    private fun malformed(message: String): Nothing = throw AtlasWireException.Malformed(message)

    private fun JSONObject.require(key: String): Any =
        if (has(key)) get(key) else malformed("Missing field '$key'")

    private fun JSONObject.isAbsentOrNull(key: String): Boolean = !has(key) || isNull(key)

    private fun JSONObject.string(key: String): String =
        require(key) as? String ?: malformed("Field '$key' must be a string")

    private fun JSONObject.nonEmptyString(key: String): String =
        string(key).ifEmpty { malformed("Field '$key' must not be empty") }

    private fun JSONObject.optionalString(key: String): String? =
        if (isAbsentOrNull(key)) null else string(key)

    private fun JSONObject.bootId(key: String): String =
        string(key).also { if (!BOOT_ID.matches(it)) malformed("Field '$key' is not a boot ID") }

    private fun JSONObject.boolean(key: String): Boolean =
        require(key) as? Boolean ?: malformed("Field '$key' must be a boolean")

    private fun JSONObject.optionalBoolean(key: String): Boolean =
        if (isAbsentOrNull(key)) false else boolean(key)

    private fun JSONObject.obj(key: String): JSONObject =
        require(key) as? JSONObject ?: malformed("Field '$key' must be an object")

    private fun JSONObject.array(key: String): JSONArray =
        require(key) as? JSONArray ?: malformed("Field '$key' must be an array")

    private fun JSONObject.long(key: String): Long = wholeNumber(require(key), key)

    private fun JSONObject.int(key: String): Int = toInt(long(key), key)

    private fun JSONObject.uint32(key: String): Long =
        long(key).also { if (it !in 0..UINT32_MAX) malformed("Field '$key' is outside 0..$UINT32_MAX") }

    /** Required key whose value may be null (e.g. lobby `life`). */
    private fun JSONObject.nullableInt(key: String): Int? {
        require(key)
        return if (isNull(key)) null else int(key)
    }

    /** Key that may be absent or null (schema "integer or null", not in `required`). */
    private fun JSONObject.optionalInt(key: String): Int? = if (isAbsentOrNull(key)) null else int(key)

    private fun JSONArray.objects(): List<JSONObject> = List(length()) { index ->
        get(index) as? JSONObject ?: malformed("Array entry $index must be an object")
    }

    private fun JSONArray.intAt(index: Int, name: String): Int =
        toInt(wholeNumber(get(index), "$name[$index]"), "$name[$index]")

    private fun wholeNumber(value: Any, key: String): Long = when (value) {
        is Int -> value.toLong()
        is Long -> value
        is Number -> {
            val asDouble = value.toDouble()
            if (asDouble % 1.0 != 0.0 || asDouble !in Long.MIN_VALUE.toDouble()..Long.MAX_VALUE.toDouble()) {
                malformed("Field '$key' must be a whole number")
            }
            asDouble.toLong()
        }
        else -> malformed("Field '$key' must be a number")
    }

    private fun toInt(value: Long, key: String): Int =
        if (value in Int.MIN_VALUE..Int.MAX_VALUE) value.toInt() else malformed("Field '$key' is out of range")
}
