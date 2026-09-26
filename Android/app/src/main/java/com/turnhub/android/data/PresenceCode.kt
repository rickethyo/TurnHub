package com.turnhub.android.data

/**
 * The six-digit table-presence code from a scanned Atlas QR: the Admin-code
 * screen encodes `http://192.168.4.1/portal#code=123456`. A bare six-digit
 * text is accepted too. Anything else (the Wi-Fi or portal QR) is null.
 */
fun presenceCodeFromQr(text: String): String? {
    val trimmed = text.trim()
    Regex("""^\d{6}$""").matchEntire(trimmed)?.let { return it.value }
    return Regex("""[#?&]code=(\d{6})(?:$|&)""").find(trimmed)?.groupValues?.get(1)
}
