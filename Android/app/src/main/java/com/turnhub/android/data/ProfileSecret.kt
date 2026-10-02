package com.turnhub.android.data

/**
 * A profile's secret: a PIN (4 to 8 digits) or a password (8 to 64 UTF-8
 * bytes, no control characters). Atlas checks the same rule (`validPin` in
 * Atlas/src/web_session.cpp) and is the authority; this only spares a round
 * trip for an entry Atlas would refuse.
 */
object ProfileSecret {
    const val MAX_CHARS = 64
    const val RULE = "Use a 4–8 digit PIN or a password of 8 to 64 characters."

    fun isValid(secret: String): Boolean {
        if (secret.any { it.code < 0x20 || it.code == 0x7F }) return false
        if (secret.length in 4..8 && secret.all { it in '0'..'9' }) return true
        return secret.toByteArray(Charsets.UTF_8).size in 8..64
    }
}
