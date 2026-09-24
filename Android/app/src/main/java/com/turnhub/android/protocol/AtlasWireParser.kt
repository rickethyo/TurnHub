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
            )
        }
    }

    fun parseState(body: String): StateSnapshot {
        val root = parseObject(body) { AtlasWireException.Malformed("State response is not JSON") }
        return wrap {
            val settings = root.obj("settings")
            val profileKey = settings.string("profile")
            val stateKey = root.string("state")
            StateSnapshot(
                protocolVersion = root.string("protocolVersion"),
                atlasId = root.nonEmptyString("atlasId"),
                bootId = root.bootId("bootId"),
                revision = root.uint32("revision"),
                state = TableState.fromWire(stateKey) ?: malformed("Unknown table state '$stateKey'"),
                hostModuleId = root.optionalInt("hostModuleId"),
                starterPlayer = root.optionalInt("starterPlayer"),
                activePlayer = root.optionalInt("activePlayer"),
                winnerPlayer = root.optionalInt("winnerPlayer"),
                settings = TableSettings(
                    profile = GameProfile.fromWire(profileKey) ?: malformed("Unknown game profile '$profileKey'"),
                    startingLife = settings.int("startingLife"),
                ),
                sampledAtMs = root.uint32("sampledAtMs"),
                gameElapsedMs = root.uint32("gameElapsedMs"),
                turnElapsedMs = root.uint32("turnElapsedMs"),
                pending = parsePending(root.obj("pending")),
                players = root.array("players").objects().map(::parsePlayer),
            )
        }
    }

    /** `GET /api/seats`: names per seat. Blank names are reported as null. */
    fun parseSeats(body: String): List<SeatEntry> {
        val root = parseObject(body) { AtlasWireException.Malformed("Seats response is not JSON") }
        return wrap {
            root.array("seats").objects().map { seat ->
                SeatEntry(
                    moduleId = seat.int("module"),
                    slot = seat.int("slot"),
                    playerNumber = seat.int("player"),
                    name = seat.optionalString("name")?.trim()?.takeIf { it.isNotEmpty() },
                )
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
                moduleId = root.int("module"),
                slot = root.int("slot"),
                playerNumber = root.int("player"),
                participating = root.boolean("participating"),
                host = root.boolean("host"),
                active = root.boolean("active"),
                eliminated = root.boolean("eliminated"),
            )
        }
    }

    /** Control results and plain `{"ok":..,"error":..}` bodies; lenient because error shapes vary. */
    fun parseControlResult(body: String): ControlResult {
        val root = parseObject(body) { AtlasWireException.Malformed("Control response is not JSON") }
        return wrap {
            ControlResult(
                ok = root.optBoolean("ok", false),
                status = root.optionalString("status"),
                message = root.optionalString("message") ?: root.optionalString("error"),
                revision = if (root.isAbsentOrNull("revision")) null else root.uint32("revision"),
                bootId = root.optionalString("bootId"),
            )
        }
    }

    /** The `error` text of an Atlas error body, if it has one. */
    fun errorMessage(body: String): String? = try {
        JSONObject(body.trim()).optString("error").takeIf { it.isNotBlank() }
    } catch (_: JSONException) {
        null
    }

    private fun parsePending(pending: JSONObject) = PendingDecisions(
        passPlayer = pending.optionalInt("passPlayer"),
        passGraceRemainingMs = if (pending.isAbsentOrNull("passGraceRemainingMs")) 0L
        else pending.uint32("passGraceRemainingMs"),
        winClaimPlayer = pending.optionalInt("winClaimPlayer"),
        winConfirmationPlayer = pending.optionalInt("winConfirmationPlayer"),
        eliminationTargetPlayer = pending.optionalInt("eliminationTargetPlayer"),
    )

    private fun parsePlayer(player: JSONObject) = Player(
        playerNumber = player.int("playerNumber"),
        moduleId = player.int("moduleId"),
        slot = player.int("slot"),
        participantId = player.uint32("participantId"),
        profileId = player.optionalString("profileId"),
        displayName = player.optionalString("displayName"),
        eliminated = player.boolean("eliminated"),
        life = player.nullableInt("life"),
        turnsCompleted = player.uint32("turnsCompleted"),
        commanderDamage = player.array("commanderDamage").objects().map { entry ->
            val damage = entry.array("damage")
            if (damage.length() != 2) malformed("commanderDamage.damage must have two entries")
            CommanderDamage(
                sourcePlayer = entry.int("sourcePlayer"),
                damage = List(2) { index -> damage.intAt(index, "damage") },
            )
        },
        lifeRequest = if (player.isNull("lifeRequest")) {
            player.require("lifeRequest") // Required, but may be null.
            null
        } else {
            val request = player.obj("lifeRequest")
            val stateKey = request.string("state")
            LifeRequest(
                id = request.uint32("id"),
                actor = request.int("actor"),
                target = request.int("target"),
                delta = request.int("delta"),
                state = LifeRequestState.fromWire(stateKey)
                    ?: malformed("Unknown life request state '$stateKey'"),
                requestedAtMs = request.uint32("requestedAtMs"),
            )
        },
    )

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
