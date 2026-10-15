package com.turnhub.android.data

import com.turnhub.android.standalone.PlanarCard
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withContext
import org.json.JSONArray
import org.json.JSONObject
import java.io.File
import java.io.IOException
import java.net.URLEncoder

/** One plane or phenomenon as Scryfall describes it: what the Planes tab shows, with the artist credit. */
data class PlaneInfo(
    /** Scryfall's card id, which is also the [PlanarCard.id] in the deck. */
    val id: String,
    val name: String,
    val typeLine: String,
    val text: String,
    val artist: String,
    val artCrop: String,
    val setName: String,
) {
    val phenomenon: Boolean get() = typeLine.trim().lowercase().startsWith("phenomenon")
    val card: PlanarCard get() = PlanarCard(id, phenomenon)
}

/**
 * Downloads the planes and phenomena (names, rules text and art) from Scryfall
 * (https://scryfall.com/docs/api) to this phone, the first time Planechase is
 * turned on, and keeps them: TurnHub ships no card content itself (owner
 * decision 2026-10-15; Documentation/legal/DEPENDENCY_TRACKER.md). Nothing about
 * the players is sent. Requests are spaced out and carry the headers Scryfall asks for.
 */
class ScryfallPlanes(
    /** Kept (app files): the list of cards. */
    private val dataDir: File,
    /** Re-fetchable (app cache): each card's art. */
    private val artDir: File,
    private val userAgent: String,
    private val baseUrl: String = "https://api.scryfall.com",
    private val opener: HttpConnectionOpener = HttpConnectionOpener.Default,
    private val ioDispatcher: CoroutineDispatcher = Dispatchers.IO,
) {
    private val gate = Mutex()
    private val http = ScryfallHttp(userAgent, opener)
    private val catalogue get() = File(dataDir, "planes.json")

    /** The downloaded cards, or null before the first download. */
    fun saved(): List<PlaneInfo>? = runCatching {
        val json = JSONObject(catalogue.readText())
        val cards = json.getJSONArray("cards")
        (0 until cards.length()).map { fromJson(cards.getJSONObject(it)) }
    }.getOrNull()

    /** Fetches every paper plane and phenomenon (Un- cards left out) and replaces the saved list. */
    suspend fun download(): List<PlaneInfo> = withContext(ioDispatcher) {
        gate.withLock {
            val cards = mutableListOf<PlaneInfo>()
            var page: String? = "$baseUrl/cards/search?unique=cards&order=name&q=" + URLEncoder.encode(QUERY, "UTF-8")
            var pages = 0
            while (page != null) {
                if (++pages > MAX_PAGES) throw IOException("Scryfall returned too many pages")
                val json = JSONObject(http.fetch(page, MAX_JSON_BYTES).toString(Charsets.UTF_8))
                val data = json.getJSONArray("data")
                for (i in 0 until data.length()) parse(data.getJSONObject(i))?.let { cards += it }
                page = json.optString("next_page").takeIf { json.optBoolean("has_more") && it.startsWith(baseUrl) }
                delay(ScryfallHttp.REQUEST_SPACING_MS)
            }
            if (cards.none { !it.phenomenon }) throw IOException("Scryfall returned no planes")
            dataDir.mkdirs()
            val tmp = File(dataDir, "planes.json.tmp")
            tmp.writeText(JSONObject().put("schema", 1).put("cards", JSONArray().apply { cards.forEach { put(toJson(it)) } }).toString())
            if (!tmp.renameTo(catalogue)) throw IOException("Couldn't save the planes")
            cards
        }
    }

    /** The card's art crop, from the cache or Scryfall; null when it can't be fetched. */
    suspend fun art(card: PlaneInfo): ByteArray? = withContext(ioDispatcher) {
        val file = File(artDir, "${card.id.filter { it.isLetterOrDigit() || it == '-' }}.jpg")
        if (file.exists()) return@withContext runCatching { file.readBytes() }.getOrNull()
        gate.withLock {
            if (file.exists()) return@withLock runCatching { file.readBytes() }.getOrNull()
            val bytes = runCatching { http.fetch(card.artCrop, MAX_ART_BYTES) }.getOrNull() ?: return@withLock null
            runCatching { artDir.mkdirs(); file.writeBytes(bytes) }
            delay(ScryfallHttp.REQUEST_SPACING_MS)
            bytes
        }
    }

    private fun parse(json: JSONObject): PlaneInfo? {
        val crop = json.optJSONObject("image_uris")?.optString("art_crop").orEmpty()
        val id = json.optString("id")
        if (id.isBlank() || !crop.startsWith("https://")) return null
        return PlaneInfo(id, json.optString("name"), json.optString("type_line"), json.optString("oracle_text"),
            json.optString("artist"), crop, json.optString("set_name"))
    }

    companion object {
        /** Every paper plane and phenomenon; joke and playtest cards aren't for official decks. */
        const val QUERY = "(t:plane or t:phenomenon) game:paper -is:funny"
        private const val MAX_PAGES = 10
        private const val MAX_JSON_BYTES = 4 * 1024 * 1024
        private const val MAX_ART_BYTES = 2 * 1024 * 1024

        private fun toJson(card: PlaneInfo) = JSONObject().put("id", card.id).put("name", card.name).put("typeLine", card.typeLine)
            .put("text", card.text).put("artist", card.artist).put("artCrop", card.artCrop).put("setName", card.setName)

        private fun fromJson(json: JSONObject) = PlaneInfo(json.getString("id"), json.getString("name"), json.getString("typeLine"),
            json.getString("text"), json.getString("artist"), json.getString("artCrop"), json.getString("setName"))
    }
}
