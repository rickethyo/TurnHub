package com.turnhub.android.data

import com.turnhub.android.standalone.SavedLocalPlayer
import com.turnhub.android.protocol.AtlasWireParser
import kotlinx.coroutines.NonCancellable
import kotlinx.coroutines.withContext

/** Credentials prove a link; a public eight-digit ID alone never proves ownership. */
class LocalAccountTransfer(private val transport: AtlasSessionTransport) {
    data class Result(val profileId: String, val warning: String? = null, val linkKey: String? = null)
    /** An Atlas account as it stands there, to recreate on this phone (fresh install or a new device). */
    data class Restored(val profileId: String, val name: String, val color: String?, val avatar: String,
                        val reduceMotion: Boolean, val linkKey: String?)

    /**
     * Signs in to an Atlas account with its PIN and reads what this phone needs to
     * recreate it: name, look and a link key, so the usual profile sync takes over.
     */
    suspend fun restore(profileId: String, secret: String): Restored {
        require(ProfileSecret.isValid(secret)) { ProfileSecret.RULE }
        val login = transport.login(profileId, secret)
        try {
            val info = transport.me(login.token)
            require(login.profileId == profileId && info.profileId == profileId) { "Atlas did not verify this account." }
            val linkKey = try {
                val response = transport.raw("POST", "/api/profiles/link-key", login.token)
                if (response.ok) org.json.JSONObject(response.body).optString("linkKey").takeIf { it.matches(Regex("[0-9A-F]{32}")) } else null
            } catch (e: kotlinx.coroutines.CancellationException) { throw e } catch (_: Exception) { null }
            // The look is a courtesy: without a card or on older firmware the account still comes back.
            val look = try { transport.getPersonalization(login.token) }
                catch (e: kotlinx.coroutines.CancellationException) { throw e } catch (_: Exception) { null }
            val avatar = look?.avatar?.takeIf { it > 0 }?.let { id ->
                try { transport.getAvatars().firstOrNull { it.id == id }?.key }
                catch (e: kotlinx.coroutines.CancellationException) { throw e } catch (_: Exception) { null }
            }
            val reduceMotion = try {
                transport.getAccessibility(login.token).ledStyle == com.turnhub.android.protocol.LedStyle.REDUCED_MOTION
            } catch (e: kotlinx.coroutines.CancellationException) { throw e } catch (_: Exception) { false }
            return Restored(profileId, info.name.orEmpty(), look?.color?.takeIf { it.startsWith("#") },
                avatar?.takeIf { it in com.turnhub.android.standalone.LocalPersonalization.AVATARS } ?: "none", reduceMotion, linkKey)
        } finally {
            withContext(NonCancellable) {
                try { transport.logout(login.token) } catch (_: Exception) { /* RAM token will expire. */ }
            }
        }
    }

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
            // Atlas 0.7.18+ gives this phone a key to keep the profile in sync; older ones don't.
            val linkKey = try {
                val response = transport.raw("POST", "/api/profiles/link-key", login.token)
                if (response.ok) org.json.JSONObject(response.body).optString("linkKey").takeIf { it.matches(Regex("[0-9A-F]{32}")) } else null
            } catch (e: kotlinx.coroutines.CancellationException) { throw e } catch (_: Exception) { null }
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
            return Result(expected, warning, linkKey)
        } finally {
            withContext(NonCancellable) {
                try { transport.logout(login.token) } catch (_: Exception) { /* RAM token will expire. */ }
            }
        }
    }
}
