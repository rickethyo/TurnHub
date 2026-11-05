package com.turnhub.android.data

/**
 * The six-digit table-presence code from a scanned Atlas QR: the Admin-code
 * screen encodes six digits, including any leading zeros. Code links are
 * accepted too. Anything else (including the Wi-Fi QR) is null.
 */
fun presenceCodeFromQr(text: String): String? {
    val trimmed = text.trim()
    Regex("""^\d{6}$""").matchEntire(trimmed)?.let { return it.value }
    return Regex("""[#?&]code=(\d{6})(?:$|&)""").find(trimmed)?.groupValues?.get(1)
}
