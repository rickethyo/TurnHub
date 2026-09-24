package com.turnhub.android.protocol

/**
 * Wire models for Atlas's existing profile/session routes (protocol/http-v1.md
 * "Connection and first PASS"). These predate the versioned v1 contract, so
 * only the fields the app uses are modeled.
 */

/** One entry of `GET /api/profiles`. Public: no PIN or statistics. */
data class ProfileSummary(
    val profileId: String,
    val name: String,
    /** Browser/app sign-in needs a PIN; PIN-less profiles sign in physically. */
    val hasPin: Boolean,
)

/** `POST /api/session/login` success. [token] goes in `X-TurnHub-Token`; keep it private. */
data class LoginResult(val token: String, val profileId: String) {
    override fun toString(): String = "LoginResult(token=***, profileId=$profileId)"
}

/**
 * `GET /api/session/me`: what Atlas resolved this session to. Atlas decides
 * the seat and player from the session; the app never supplies them.
 */
data class SessionInfo(
    val profileId: String,
    val name: String?,
    /** Controller handle the session resolved to (virtual 8-23 for a phone-only player). */
    val moduleId: Int,
    val slot: Int,
    /** 0 when not at the table. */
    val playerNumber: Int,
    val participating: Boolean,
    val host: Boolean,
    val active: Boolean,
    val eliminated: Boolean,
)

/**
 * Result of a session control such as `POST /api/control/pass`. [status] is
 * ACCEPTED, REJECTED or CONFLICT when Atlas got that far; earlier validation
 * failures carry only [message].
 */
data class ControlResult(
    val ok: Boolean,
    val status: String?,
    val message: String?,
    val revision: Long?,
    val bootId: String?,
)
