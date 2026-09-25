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
}

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
}

fun interface AtlasSessionTransportFactory {
    fun create(endpoint: AtlasEndpoint): AtlasSessionTransport
}
