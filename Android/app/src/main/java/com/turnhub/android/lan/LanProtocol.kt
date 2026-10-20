package com.turnhub.android.lan

import org.json.JSONObject
import java.net.URI
import java.security.MessageDigest
import java.security.SecureRandom

internal const val LAN_VERSION = 1
internal const val LAN_MAX_FRAME = 512 * 1024

/**
 * Where a guest reaches a host and what it presents. [key] is the bearer secret: the
 * short join code in standard joining, or a long random key that only the QR carries in
 * QR-only joining. Never log it or keep it in saved state.
 */
internal data class LanInvite(val address: String, val port: Int, val pin: String, val session: String, val key: String) {
    fun encode() = "turnhub://join/$LAN_VERSION/$address/$port/$pin/$session/$key"
    override fun toString() = "LanInvite($address:$port)"

    companion object {
        fun parse(text: String): LanInvite {
            require(text.length <= 512)
            val uri = URI(text.trim())
            require(uri.scheme == "turnhub" && uri.host == "join" && uri.rawQuery == null && uri.fragment == null && uri.userInfo == null)
            val p = uri.path.split('/').drop(1)
            require(p.size == 6 && p[0] == "$LAN_VERSION" && privateIpv4(p[1]))
            val port = p[2].toIntOrNull()?.takeIf { it in 1024..65535 } ?: error("Invalid port")
            require(p[3].matches(Regex("[0-9a-f]{64}")) && p[4].matches(Regex("[0-9a-f]{32}")) && p[5].matches(Regex("[0-9a-f]{64}")))
            return LanInvite(p[1], port, p[3], p[4], p[5])
        }
    }
}

/**
 * A host's join code: three letters and three digits, like `abc-123`. Letters that read as
 * digits (i, l, o) are never used. It stays the same while the host keeps sharing.
 */
internal object LanJoinCode {
    private const val LETTERS = "abcdefghjkmnpqrstuvwxyz"
    fun generate(random: SecureRandom = SecureRandom()): String =
        (1..3).map { LETTERS[random.nextInt(LETTERS.length)] }.joinToString("") + "-" + (1..3).joinToString("") { "${random.nextInt(10)}" }
    /** "ABC 123", "abc123" and "abc-123" all read as "abc-123"; null when it isn't a code. */
    fun normalize(text: String): String? {
        val chars = text.lowercase().filter { it.isLetterOrDigit() }
        if (chars.length != 6 || !chars.take(3).all { it in LETTERS } || !chars.drop(3).all { it in '0'..'9' }) return null
        return chars.take(3) + "-" + chars.drop(3)
    }
}

internal fun privateIpv4(address: String): Boolean {
    val parts = address.split('.')
    if (parts.size != 4 || parts.any { !it.matches(Regex("0|[1-9][0-9]{0,2}")) || it.toInt() !in 0..255 }) return false
    val a = parts.map(String::toInt)
    return a[0] == 10 || (a[0] == 172 && a[1] in 16..31) ||
        (a[0] == 192 && a[1] == 168) || (a[0] == 169 && a[1] == 254)
}

internal fun randomLanSecret(bytes: Int = 32): String = ByteArray(bytes).also { SecureRandom().nextBytes(it) }.hex()
internal fun ByteArray.hex() = joinToString("") { "%02x".format(it.toInt() and 255) }
internal fun sameSecret(a: String, b: String) = MessageDigest.isEqual(a.toByteArray(Charsets.UTF_8), b.toByteArray(Charsets.UTF_8))
internal fun JSONObject.text(key: String): String = (get(key) as? String) ?: error("Expected text")
internal fun JSONObject.integer(key: String): Long {
    val value = get(key)
    require(value is Int || value is Long)
    return (value as Number).toLong()
}

/**
 * Planar actions (`planar-roll`, `planar-walk`, ...) act for the active player, as the
 * host's Planes tab does, so a seat or a planes display can send them.
 */
internal const val PLANAR_KIND = "planar-"

/** The host infers the player from the authenticated session, never a request's seat. */
internal data class LanAction(val match: String, val revision: Long, val player: Int, val kind: String, val value: Int, val slot: Int, val source: Int = 0)

internal val com.turnhub.android.protocol.GameProfile.lanSupported: Boolean
    get() = deathMayDie || magicRules || this in listOf(com.turnhub.android.protocol.GameProfile.YUGIOH,
        com.turnhub.android.protocol.GameProfile.POKEMON, com.turnhub.android.protocol.GameProfile.LORCANA)
/** [name] keeps a player's seat when lobby changes move them to another number. */
internal data class LanGuest(val token: String, val role: String, val player: Int, val name: String? = null) {
    override fun toString() = "LanGuest($role, player=$player)"
}
