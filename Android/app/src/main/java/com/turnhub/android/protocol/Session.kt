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
    /** IDs redirected here by an explicit Atlas account merge. */
    val aliases: List<String> = emptyList(),
)

/** `POST /api/session/login` success. [token] goes in `X-TurnHub-Token`; keep it private. */
data class LoginResult(val token: String, val profileId: String) {
    override fun toString(): String = "LoginResult(token=***, profileId=$profileId)"
}

/** `GET /api/session/me`: the signed-in profile and its account permissions. */
data class SessionInfo(
    val profileId: String,
    val name: String?,
    /** Account permission bits (see [AccountPermission]). */
    val permissions: Int = 0,
) {
    fun has(permission: AccountPermission): Boolean = permissions and permission.bit != 0
}

/** Atlas account permissions (`permissions` in `/api/session/me` and `/api/accounts`). */
enum class AccountPermission(val bit: Int, val label: String) {
    ADMIN(1, "Admin"),
    GAME_MASTER(2, "Game Master"),
    DEVELOPER(4, "Developer"),
    GM_RESET_CONNECTIONS(8, "GM: reset connections"),
    GM_REMOVE_FROM_GAME(16, "GM: remove from game"),
}

