package com.turnhub.android.standalone

import com.turnhub.android.protocol.AvatarIcon
import java.security.SecureRandom
import java.security.MessageDigest
import javax.crypto.SecretKeyFactory
import javax.crypto.spec.PBEKeySpec
import java.util.Base64
import org.json.JSONObject

/** Local UUID owns history; the compatible public ID is never proof of account ownership. */
data class AtlasProfileLink(val atlasId: String, val profileId: String)

data class LocalPersonalization(
    val color: String? = null,
    val avatar: String = "none",
    val image: String? = null,
    val reduceMotion: Boolean = false,
    /** Phone-computed RGB332 thumbnail; Atlas never decodes the full JPEG. */
    val thumbnail: String? = null,
) {
    fun icon(): AvatarIcon? = when {
        image != null -> AvatarIcon(128, "custom", "Player image", emptyList(), Base64.getDecoder().decode(image))
        avatar != "none" -> AvatarIcon(1, avatar, avatar.replaceFirstChar { it.uppercase() }, emptyList())
        else -> null
    }
    fun toJson() = JSONObject().put("color", color ?: JSONObject.NULL).put("avatar", avatar)
        .put("image", image ?: JSONObject.NULL).put("reduceMotion", reduceMotion).put("thumbnail", thumbnail ?: JSONObject.NULL)

    companion object {
        val AVATARS = listOf("none", "die", "crown", "sword", "shield", "star", "moon", "flame", "tree", "cat", "skull", "gem", "bolt")
        const val MAX_IMAGE_BYTES = 48 * 1024
        fun fromJson(json: JSONObject) = LocalPersonalization(json.optStringOrNull("color"),
            json.optString("avatar", "none"), json.optStringOrNull("image"), json.optBoolean("reduceMotion"), json.optStringOrNull("thumbnail")).also {
            require(it.color == null || it.color.matches(Regex("#[0-9a-fA-F]{6}")))
            require(it.avatar in AVATARS)
            require(it.image == null || Base64.getDecoder().decode(it.image).size in 1..MAX_IMAGE_BYTES)
            require(it.thumbnail == null || it.thumbnail.matches(Regex("[0-9a-fA-F]{512}")))
        }
    }
}

object LocalProfileIds {
    private val random = SecureRandom()
    fun create(): String = "%08X".format(java.util.Locale.ROOT, random.nextInt())
    fun valid(id: String) = id.matches(Regex("[0-9A-F]{8}"))
}

/** Optional local account credential. Only salted verifier bytes enter saved data. */
data class LocalCredential(val salt: String, val hash: String) {
    fun verifies(secret: String): Boolean = runCatching {
        MessageDigest.isEqual(Base64.getDecoder().decode(hash), derive(secret, Base64.getDecoder().decode(salt)))
    }.getOrDefault(false)
    companion object {
        fun create(secret: String): LocalCredential {
            require(com.turnhub.android.data.ProfileSecret.isValid(secret))
            val salt = ByteArray(16).also { SecureRandom().nextBytes(it) }
            return LocalCredential(Base64.getEncoder().encodeToString(salt), Base64.getEncoder().encodeToString(derive(secret, salt)))
        }
        private fun derive(secret: String, salt: ByteArray): ByteArray {
            val spec = PBEKeySpec(secret.toCharArray(), salt, 120_000, 256)
            return try { SecretKeyFactory.getInstance("PBKDF2WithHmacSHA256").generateSecret(spec).encoded }
            finally { spec.clearPassword() }
        }
    }
}
