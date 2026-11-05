package com.turnhub.android.data

/**
 * The six-digit table-presence code from a scanned Atlas QR: the Admin-code
 * screen encodes six digits, including any leading zeros. HTTPS code links are
 * accepted too; plaintext HTTP links and anything else (including the Wi-Fi
 * QR) are null.
 */
fun presenceCodeFromQr(text: String): String? {
    val trimmed = text.trim()
    Regex("""^\d{6}$""").matchEntire(trimmed)?.let { return it.value }
    if (trimmed.startsWith("http://", ignoreCase = true)) return null
    return Regex("""[#?&]code=(\d{6})(?:$|&)""").find(trimmed)?.groupValues?.get(1)
}
