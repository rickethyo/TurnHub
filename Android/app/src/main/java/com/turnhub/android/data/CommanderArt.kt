package com.turnhub.android.data

import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withContext
import org.json.JSONObject
import java.io.ByteArrayOutputStream
import java.io.File
import java.io.IOException
import java.net.HttpURLConnection
import java.net.URL
import java.net.URLEncoder
import java.security.MessageDigest

/** A commander's art crop and the credit Scryfall asks us to show with it. */
class CommanderArt(val cardName: String, val artist: String, val jpeg: ByteArray)

/** One printing of a card with its own art, for choosing which art a tile shows. */
class CommanderPrinting(val id: String, val setName: String, val artist: String, val artCrop: String)

/**
 * Looks up a named commander's art on Scryfall (https://scryfall.com/docs/api):
 * only the name the player typed is sent. Results, misses included, are kept
 * in [cacheDir] so each name is asked for once. Requests are spaced out as
 * Scryfall asks and use the phone's default network, like firmware updates.
 */
class ScryfallCommanderArt(
    private val cacheDir: File,
    private val userAgent: String,
    private val baseUrl: String = "https://api.scryfall.com",
    private val opener: HttpConnectionOpener = HttpConnectionOpener.Default,
    private val ioDispatcher: CoroutineDispatcher = Dispatchers.IO,
) {
    private val gate = Mutex()

    /**
     * The art for [name] (the [printing] the player picked, else Scryfall's default),
     * from the cache or Scryfall; null when no card matches or the lookup fails.
     */
    suspend fun art(name: String, printing: String? = null): CommanderArt? = withContext(ioDispatcher) {
        val id = printing?.takeIf { it.matches(PRINTING_ID) }
        val key = cacheKey(if (id == null) name else "$name#$id")
        cached(key)?.let { return@withContext it.takeIf { art -> art.jpeg.isNotEmpty() } }
        gate.withLock {
            cached(key)?.let { return@withLock it.takeIf { art -> art.jpeg.isNotEmpty() } }
            val card = runCatching { lookup(name, id) }.getOrElse { error ->
                // A card Scryfall can't find is remembered; a network failure is tried again later.
                if (error is NotFound) remember(key, CommanderArt(name, "", ByteArray(0)))
                return@withLock null
            }
            delay(REQUEST_SPACING_MS)
            val art = runCatching { CommanderArt(card.name, card.artist, fetch(card.artCrop, MAX_ART_BYTES)) }.getOrNull()
                ?: return@withLock null
            remember(key, art)
            delay(REQUEST_SPACING_MS)
            art
        }
    }

    private class Card(val name: String, val artist: String, val artCrop: String)
    private class NotFound : IOException("No card matches")

    /** Card names starting like [typed], for the commander picker; empty under two letters or offline. */
    suspend fun suggest(typed: String): List<String> = withContext(ioDispatcher) {
        val query = typed.trim()
        if (query.length < 2) return@withContext emptyList()
        gate.withLock {
            val names = runCatching {
                val json = JSONObject(fetch("$baseUrl/cards/autocomplete?q=" + URLEncoder.encode(query, "UTF-8"), MAX_JSON_BYTES)
                    .toString(Charsets.UTF_8)).optJSONArray("data")
                (0 until (json?.length() ?: 0)).map { json!!.getString(it) }
            }.getOrDefault(emptyList())
            delay(REQUEST_SPACING_MS)
            names
        }
    }

    /** Each distinct art [name] was printed with, newest first; empty when unknown or offline. */
    suspend fun printings(name: String): List<CommanderPrinting> = withContext(ioDispatcher) {
        gate.withLock {
            val found = runCatching {
                val query = "!\"" + name.trim().replace("\"", "") + "\" game:paper"
                val url = "$baseUrl/cards/search?unique=art&order=released&dir=desc&q=" + URLEncoder.encode(query, "UTF-8")
                val data = JSONObject(fetch(url, MAX_LIST_BYTES).toString(Charsets.UTF_8)).optJSONArray("data")
                (0 until (data?.length() ?: 0)).mapNotNull { i ->
                    val card = data!!.getJSONObject(i)
                    val face = frontFace(card) ?: return@mapNotNull null
                    val crop = face.optJSONObject("image_uris")?.optString("art_crop").orEmpty()
                    val id = card.optString("id")
                    if (!crop.startsWith("https://") || !id.matches(PRINTING_ID)) null
                    else CommanderPrinting(id, card.optString("set_name"), face.optString("artist", card.optString("artist")), crop)
                }.take(MAX_PRINTINGS)
            }.getOrDefault(emptyList())
            delay(REQUEST_SPACING_MS)
            found
        }
    }

    /** A printing's art crop for the picker, cached like tile art; null offline. */
    suspend fun preview(printing: CommanderPrinting): ByteArray? = withContext(ioDispatcher) {
        val file = File(cacheDir, cacheKey("preview#${printing.id}") + ".jpg")
        if (file.exists()) return@withContext file.readBytes()
        // Card images come from Scryfall's image host, which asks for no request spacing.
        runCatching { fetch(printing.artCrop, MAX_ART_BYTES) }.getOrNull()?.also { jpeg ->
            runCatching { cacheDir.mkdirs(); file.writeBytes(jpeg) }
        }
    }

    private fun lookup(name: String, printing: String?): Card {
        val url = if (printing != null) "$baseUrl/cards/$printing"
            else "$baseUrl/cards/named?fuzzy=" + URLEncoder.encode(name.trim(), "UTF-8")
        val json = JSONObject(fetch(url, MAX_JSON_BYTES).toString(Charsets.UTF_8))
        val face = frontFace(json) ?: throw NotFound()
        val crop = face.optJSONObject("image_uris")?.optString("art_crop").orEmpty()
        if (!crop.startsWith("https://")) throw NotFound()
        return Card(json.optString("name", name), face.optString("artist", json.optString("artist")), crop)
    }

    /** Double-faced cards keep their art on each face; the front face is the commander. */
    private fun frontFace(card: JSONObject): JSONObject? =
        card.optJSONObject("image_uris")?.let { card } ?: card.optJSONArray("card_faces")?.optJSONObject(0)

    private fun fetch(url: String, limit: Int): ByteArray {
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

    private fun cached(key: String): CommanderArt? = runCatching {
        val meta = File(cacheDir, "$key.json").takeIf { it.exists() } ?: return null
        val json = JSONObject(meta.readText())
        val image = File(cacheDir, "$key.jpg")
        CommanderArt(json.getString("name"), json.getString("artist"), if (image.exists()) image.readBytes() else ByteArray(0))
    }.getOrNull()

    private fun remember(key: String, art: CommanderArt) {
        runCatching {
            cacheDir.mkdirs()
            if (art.jpeg.isNotEmpty()) File(cacheDir, "$key.jpg").writeBytes(art.jpeg)
            File(cacheDir, "$key.json").writeText(JSONObject().put("name", art.cardName).put("artist", art.artist).toString())
        }
    }

    companion object {
        private const val REQUEST_SPACING_MS = 100L
        private const val MAX_JSON_BYTES = 256 * 1024
        private const val MAX_ART_BYTES = 2 * 1024 * 1024
        private const val MAX_LIST_BYTES = 4 * 1024 * 1024
        private const val MAX_PRINTINGS = 30
        private val PRINTING_ID = com.turnhub.android.standalone.StandaloneGame.PRINTING_ID

        /** Same name, same file, however it was capitalised or spaced. */
        fun cacheKey(name: String): String = MessageDigest.getInstance("SHA-256")
            .digest(name.trim().lowercase().replace(Regex("\\s+"), " ").toByteArray())
            .take(16).joinToString("") { "%02x".format(it) }
    }
}
