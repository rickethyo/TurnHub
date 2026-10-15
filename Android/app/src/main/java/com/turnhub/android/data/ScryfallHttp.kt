package com.turnhub.android.data

import java.io.ByteArrayOutputStream
import java.io.IOException
import java.net.HttpURLConnection
import java.net.URL

/**
 * One request to Scryfall (https://scryfall.com/docs/api), shared by commander
 * art and the Planechase planes: the User-Agent and Accept headers Scryfall
 * requires, timeouts, and a cap on how much an answer may be.
 */
class ScryfallHttp(private val userAgent: String, private val opener: HttpConnectionOpener = HttpConnectionOpener.Default) {
    /** Scryfall answered 404: no card matches. */
    class NotFound : IOException("No card matches")

    fun fetch(url: String, limit: Int): ByteArray {
        val connection = opener.open(URL(url))
        try {
            connection.connectTimeout = 10_000
            connection.readTimeout = 20_000
            connection.instanceFollowRedirects = true
            // Scryfall requires both headers on every request.
            connection.setRequestProperty("User-Agent", userAgent)
            connection.setRequestProperty("Accept", "application/json;q=0.9,*/*;q=0.8")
            val code = connection.responseCode
            if (code == HttpURLConnection.HTTP_NOT_FOUND) throw NotFound()
            if (code !in 200..299) throw IOException("Scryfall answered HTTP $code")
            val out = ByteArrayOutputStream()
            connection.inputStream.use { input ->
                val buffer = ByteArray(16 * 1024)
                while (true) {
                    val read = input.read(buffer)
                    if (read < 0) break
                    out.write(buffer, 0, read)
                    if (out.size() > limit) throw IOException("Scryfall's answer is too large")
                }
            }
            return out.toByteArray()
        } finally {
            connection.disconnect()
        }
    }

    companion object {
        /** Scryfall asks for 50 to 100 ms between requests. */
        const val REQUEST_SPACING_MS = 100L
    }
}
