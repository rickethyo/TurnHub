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
import com.turnhub.android.protocol.StateSnapshot

/**
 * The wire operations [HttpAtlasRepository] needs from one Atlas endpoint.
 * Implementations throw [AtlasException] for every failure they can classify
 * (network, HTTP status, not-TurnHub, malformed body).
 *
 * This is deliberately a narrow seam so repository behavior can be tested
 * against a fake without a network. Authenticated session calls
 * (profiles, login, join, `/api/session/me`, PASS) will be added here in the
 * next milestone; `/api/v1/intent` and events do not exist yet.
 */
interface AtlasTransport {
    /** `GET /api/v1/info`. */
    suspend fun getInfo(): AtlasInfo

    /** `GET /api/v1/state`. */
    suspend fun getState(): StateSnapshot

    /** `GET /api/seats`: display names, which the v1 state snapshot doesn't carry. */
    suspend fun getSeats(): List<SeatEntry>

    /** `GET /api/avatars`: preset avatar icons. Older Atlas firmware has none. */
    suspend fun getAvatars(): List<AvatarIcon> = emptyList()
}

/** Builds a transport for the endpoint the user chose. */
fun interface AtlasTransportFactory {
    fun create(endpoint: AtlasEndpoint): AtlasTransport
}

/** The session controls the app can send (Atlas's form-based control routes, e.g. `/api/control/pass`). */
enum class ControlAction(val path: String) {
    PASS("/api/control/pass"),
    PAUSE_RESUME("/api/control/pause"),
    CLAIM_WIN("/api/control/win"),
    CONFIRM_WIN("/api/control/confirm"),
    DENY_WIN("/api/control/deny"),
    CONCEDE("/api/control/concede"),
    SELECT_STARTER("/api/control/starter"),
    START("/api/control/start"),
    CANCEL_START("/api/control/cancel-start"),
    REMATCH("/api/control/rematch"),
    RESET("/api/control/reset"),
    LEAVE("/api/session/leave"),
}

/** The signed-in profile's look at the table (`/api/session/personalization`). */
data class Personalization(
    /** `#rrggbb`, or null for the standard Sigil colors. */
    val color: String?,
    /** Preset avatar ID; 0 = none. */
    val avatar: Int,
    /** False when Atlas has no microSD card (color and avatar cannot be saved). */
    val cardPresent: Boolean,
)

/** An unparsed Atlas response. */
data class RawResponse(val code: Int, val body: String) {
    val ok: Boolean get() = code in 200..299
}

private fun unsupported(): Nothing =
    throw AtlasException(AtlasFailure.Unexpected("This Atlas connection does not support that yet"))

/**
 * Atlas's authenticated profile/session routes (protocol/http-v1.md). Atlas
 * resolves the player from the session token; nothing here sends a seat or
 * player number. Failures throw [AtlasException]; an expired or unknown token
 * is [AtlasFailure.SessionExpired], and a refusal with Atlas's reason is
 * [AtlasFailure.Rejected].
 */
interface AtlasSessionTransport {
    /** `GET /api/profiles` (public). */
    suspend fun getProfiles(): List<ProfileSummary>

    /** `POST /api/session/login` with `profileId` and `pin`. */
    suspend fun login(profileId: String, pin: String): LoginResult

    /** `GET /api/session/me`. */
    suspend fun me(token: String): SessionInfo

    /** `POST /api/session/join`: join the table as the signed-in profile. Returns Atlas's message. */
    suspend fun join(token: String): String?

    /**
     * Sends a control once. [expectedRevision]/[expectedBootId] let Atlas
     * refuse a tap made against stale state (409 CONFLICT) before dispatch.
     * A 409 is returned as a [ControlResult], not thrown.
     */
    suspend fun control(
        token: String,
        action: ControlAction,
        expectedRevision: Long?,
        expectedBootId: String?,
    ): ControlResult

    /** `GET /api/game/settings`: the next match's setup and whether this session may edit it. */
    suspend fun getGameSettings(token: String): GameSettingsInfo

    /**
     * `POST /api/game/settings` with only `turnTimerMs` (0 = OFF). Atlas keeps
     * the other settings, validates the value and allows it only for the host
     * in the lobby. Returns Atlas's message; a refusal throws [AtlasFailure.Rejected].
     */
    suspend fun setTurnTimer(token: String, turnTimerMs: Long): String?

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
    ): AccessibilitySettings

    /** `POST /api/session/logout`: revokes this token on Atlas. */
    suspend fun logout(token: String)

    /**
     * A counter or game control with form fields, such as `/api/control/life`
     * (`delta`), `/api/control/life/request` (`target`, `delta`),
     * `/api/control/life/respond` (`requestId`, `accept`) or
     * `/api/control/commander` (`source`, `commander`, `delta`). Refusals
     * with Atlas's reason (400/403/409) come back as a [ControlResult].
     */
    suspend fun postControl(token: String, path: String, fields: List<Pair<String, String>>): ControlResult =
        unsupported()

    /** `POST /api/game/settings`; null fields keep Atlas's current values. */
    suspend fun saveGameSettings(
        token: String,
        gameProfile: String?,
        startingLife: Int?,
        turnTimerMs: Long?,
    ): String? = unsupported()

    /** `POST /api/session/profile` with a new `name` and/or `pin`. */
    suspend fun saveProfile(token: String, name: String?, pin: String?): String? = unsupported()

    suspend fun getPersonalization(token: String): Personalization = unsupported()

    /**
     * Any authenticated request, returned as-is (status and body) for the
     * admin and developer screens, which read many small routes. Never throws
     * for an HTTP status; network failures throw [AtlasException].
     */
    suspend fun raw(method: String, path: String, token: String, fields: List<Pair<String, String>> = emptyList()): RawResponse =
        unsupported()

    /** `GET /api/avatars` (public): the presets a profile may choose. */
    suspend fun getAvatars(): List<AvatarIcon> = emptyList()

    /** `POST /api/session/personalization`: `color` (`#rrggbb` or `none`) and/or `avatar`. */
    suspend fun savePersonalization(token: String, color: String?, avatar: Int?): Personalization = unsupported()
}

fun interface AtlasSessionTransportFactory {
    fun create(endpoint: AtlasEndpoint): AtlasSessionTransport
}
