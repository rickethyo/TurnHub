package com.turnhub.android.data

import com.turnhub.android.standalone.SavedLocalPlayer
import com.turnhub.android.protocol.AtlasWireParser
import kotlinx.coroutines.NonCancellable
import kotlinx.coroutines.withContext

/** Credentials prove a link; a public eight-digit ID alone never proves ownership. */
class LocalAccountTransfer(private val transport: AtlasSessionTransport) {
    data class Result(val profileId: String, val warning: String? = null)
    suspend fun verify(player: SavedLocalPlayer, targetId: String?, secret: String): Result {
        require(ProfileSecret.isValid(secret)) { ProfileSecret.RULE }
        if (targetId == null) require(withContext(kotlinx.coroutines.Dispatchers.Default) {
            player.credential?.verifies(secret) != false }) { "Enter the local account's PIN or password." }
        val login = if (targetId != null) transport.login(targetId, secret) else {
            val response = transport.raw("POST", "/api/profiles/register", "", listOf(
                "name" to player.name, "pin" to secret, "profileId" to player.profileId))
            if (!response.ok) throw AtlasException(AtlasFailure.Rejected(
                AtlasWireParser.errorMessage(response.body) ?: "Atlas could not create this account."))
            AtlasWireParser.parseLogin(response.body)
        }
        try {
            val expected = targetId ?: player.profileId
            require(login.profileId == expected) { "Atlas did not preserve the requested identity. Update Atlas before retrying." }
            val info = transport.me(login.token)
            require(info.profileId == expected) { "Atlas did not verify this account." }
            // Merging retains both histories and Atlas's existing preferences. A
            // newly created account can receive phone-owned presentation choices.
            var warning: String? = null
            if (targetId == null) {
                try {
                    val presets = transport.getAvatars()
                    val avatar = presets.firstOrNull { it.key == player.personalization.avatar }?.id ?: 0
                    transport.savePersonalization(login.token, player.personalization.color ?: "none", avatar)
                    val accessibility = transport.getAccessibility(login.token)
                    transport.saveAccessibility(login.token, accessibility.sigilSound,
                        if (player.personalization.reduceMotion) com.turnhub.android.protocol.LedStyle.REDUCED_MOTION else accessibility.ledStyle,
                        accessibility.longPressMs, accessibility.winHoldMs, accessibility.lifeApprovalMs)
                    val image = player.personalization.image
                    val thumbnail = player.personalization.thumbnail
                    if (image != null && thumbnail != null) {
                        suspend fun post(fields: List<Pair<String, String>>): org.json.JSONObject {
                            val response = transport.raw("POST", "/api/session/avatar", login.token, fields)
                            if (!response.ok) throw AtlasException(AtlasFailure.Rejected("Atlas did not accept the player image."))
                            return org.json.JSONObject(response.body)
                        }
                        val bytes = java.util.Base64.getDecoder().decode(image)
                        val revision = post(listOf("action" to "start", "size" to "${bytes.size}")).getString("revision")
                        bytes.asList().chunked(768).forEachIndexed { index, chunk ->
                            post(listOf("action" to "chunk", "revision" to revision, "index" to "$index",
                                "data" to chunk.joinToString("") { "%02x".format(it.toInt() and 255) }))
                        }
                        post(listOf("action" to "submit", "revision" to revision, "thumbnail" to thumbnail))
                        warning = "Account and image imported. Atlas's image approval policy still applies."
                    }
                } catch (e: kotlinx.coroutines.CancellationException) { throw e }
                catch (_: Exception) { warning = "Account linked. Atlas could not save all personalization; your local choices are retained." }
            }
            return Result(expected, warning)
        } finally {
            withContext(NonCancellable) {
                try { transport.logout(login.token) } catch (_: Exception) { /* RAM token will expire. */ }
            }
        }
    }
}
