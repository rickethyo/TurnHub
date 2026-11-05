package com.turnhub.android.data

import com.turnhub.android.protocol.AccessibilitySettings
import com.turnhub.android.protocol.AtlasInfo
import com.turnhub.android.protocol.LedStyle
import com.turnhub.android.protocol.LoginResult
import com.turnhub.android.protocol.ProfileSummary
import com.turnhub.android.protocol.AvatarIcon
import com.turnhub.android.protocol.SessionInfo
import com.turnhub.android.protocol.SetupStatus
import com.turnhub.android.protocol.StateSnapshot

/**
 * The wire operations [HttpAtlasRepository] needs from one Atlas endpoint.
 * Implementations throw [AtlasException] for every failure they can classify
 * (network, HTTP status, not-TurnHub, malformed body).
 *
 * This is deliberately a narrow seam so repository behavior can be tested
 * against a fake without a network.
 */
interface AvatarImageTransport {
    suspend fun getAvatarImage(path: String, token: String = ""): ByteArray = unsupported()
}

interface AtlasTransport : AvatarImageTransport {
    /** `GET /api/v1/info`. */
    suspend fun getInfo(): AtlasInfo
    /** Best-effort bookkeeping time; never the game timer or an authorization deadline. */
    suspend fun syncWallClock(epochMs: Long) {}

    /** `GET /api/v1/state`. */
    suspend fun getState(): StateSnapshot

    /** `GET /api/avatars`: preset avatar icons. */
    suspend fun getAvatars(): List<AvatarIcon> = emptyList()
}

/** Builds a transport for the endpoint the user chose. */
fun interface AtlasTransportFactory {
    fun create(endpoint: AtlasEndpoint): AtlasTransport
}

/** The signed-in profile's look at the table (`/api/session/personalization`). */
data class Personalization(
    /** `#rrggbb`, or null for the standard Sigil colors. */
    val color: String?,
    /** Preset avatar ID; 0 = none. */
    val avatar: Int,
    /** False when Atlas has no microSD card (color and avatar cannot be saved). */
    val cardPresent: Boolean,
    val customAvatar: String = "",
    val pendingAvatar: String = "",
    val customImage: ByteArray? = null,
    val pendingImage: ByteArray? = null,
    val uploadApprovalRequired: Boolean = true,
    /** 0..1 while this phone is sending an image to Atlas; null otherwise. Never sent by Atlas. */
    val uploadProgress: Float? = null,
)

/** An unparsed Atlas response. */
data class RawResponse(val code: Int, val body: String) {
    val ok: Boolean get() = code in 200..299
}

private fun unsupported(): Nothing =
    throw AtlasException(AtlasFailure.Unexpected("This Atlas connection does not support that yet"))

/**
 * Atlas's authenticated profile/session routes (protocol/http-v1.md). Failures throw [AtlasException]; an expired or unknown token
 * is [AtlasFailure.SessionExpired], and a refusal with Atlas's reason is
 * [AtlasFailure.Rejected].
 */
interface AtlasSessionTransport : AvatarImageTransport {
    /** SPKI SHA-256 observed from the active HTTPS connection, when available. */
    val tlsPeerPin: String? get() = null

    /** `GET /api/v1/info`; used to identify a setup endpoint before it is trusted. */
    suspend fun getInfo(): AtlasInfo = unsupported()

    /** `GET /api/profiles` (public). */
    suspend fun getProfiles(): List<ProfileSummary>

    /** `POST /api/session/login` with `profileId` and `pin`. */
    suspend fun login(profileId: String, pin: String): LoginResult

    /** `GET /api/session/me`. */
    suspend fun me(token: String): SessionInfo

    /** `GET /api/session/accessibility`: the signed-in profile's Sigil accessibility preferences. */
    suspend fun getAccessibility(token: String): AccessibilitySettings

    /**
     * `POST /api/session/accessibility`. Atlas validates and stores them with
     * the profile, then restyles the player's Sigil. Returns what Atlas saved;
     * a refusal throws [AtlasFailure.Rejected].
     */
    suspend fun saveAccessibility(
        token: String,
        sigilSound: Boolean,
        ledStyle: LedStyle,
        longPressMs: Int,
        winHoldMs: Int,
        lifeApprovalMs: Int,
    ): AccessibilitySettings

    /** `POST /api/session/logout`: revokes this token on Atlas. */
    suspend fun logout(token: String)

    /** `POST /api/session/profile` with a new `name` and/or `pin`; a new PIN needs [currentPin] when one is set. */
    suspend fun saveProfile(token: String, name: String?, pin: String?, currentPin: String? = null): String? = unsupported()

    suspend fun getPersonalization(token: String): Personalization = unsupported()

    /**
     * Any authenticated request, returned as-is (status and body) for the
     * admin and developer screens, which read many small routes. Never throws
     * for an HTTP status; network failures throw [AtlasException].
     */
    suspend fun raw(method: String, path: String, token: String, fields: List<Pair<String, String>> = emptyList()): RawResponse =
        unsupported()

    /**
     * [raw] POST with [fields] in the query and [body] as a `text/plain` body,
     * which Atlas reads as `plain` without URL-decoding it: for the large plane
     * uploads, whose form decoding stalled Atlas's web server for about 30 s.
     */
    suspend fun rawText(path: String, token: String, fields: List<Pair<String, String>>, body: String): RawResponse =
        raw("POST", path, token, fields + ("plain" to body))

    /** `GET /api/avatars` (public): the presets a profile may choose. */
    suspend fun getAvatars(): List<AvatarIcon> = emptyList()

    /** `POST /api/session/personalization`: `color` (`#rrggbb` or `none`) and/or `avatar`. */
    suspend fun savePersonalization(token: String, color: String?, avatar: Int?): Personalization = unsupported()

    /** `POST /api/profiles/register` (public): a new account, signed in. Atlas's refusal is [AtlasFailure.Rejected]. */
    suspend fun register(name: String, pin: String): LoginResult = unsupported()

    /** `GET /api/setup` (public): Atlas's first-run setup stage. */
    suspend fun getSetup(): SetupStatus = unsupported()

    /**
     * `POST /api/updates/latest` (public): the newest release versions
     * (`atlas`, `sigil`), so Atlas can show that an update is
     * available. Returns how many devices Atlas now counts as behind
     * (`updatesAvailable`). Atlas's refusal is [AtlasFailure.Rejected].
     */
    suspend fun reportLatestFirmware(fields: List<Pair<String, String>>): Int = unsupported()

    /**
     * A signed `.thfw` package as `multipart/form-data` (field [field]) to
     * [path]: `/api/firmware` for Atlas, `/api/sigil-firmware` to stage a
     * Sigil package. Returned as-is, like [raw].
     */
    suspend fun upload(path: String, token: String, field: String, fileName: String, bytes: ByteArray): RawResponse =
        unsupported()
}

fun interface AtlasSessionTransportFactory {
    fun create(endpoint: AtlasEndpoint): AtlasSessionTransport
}
