package com.turnhub.android.data

import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withContext
import org.json.JSONObject
import java.io.File
import java.net.URLEncoder
import java.security.MessageDigest

/** A commander's art crop and the credit Scryfall asks us to show with it. */
class CommanderArt(val cardName: String, val artist: String, val jpeg: ByteArray, val rarity: String? = null, internal val metadataCurrent: Boolean = true)

/**
 * One printing of a card with its own art, for choosing which art a tile shows. [id] is a
 * Scryfall id, or another game's prefixed id ([TileCardGame]); [artCrop] is the image to show.
 */
class CommanderPrinting(val id: String, val setName: String, val artist: String, val artCrop: String, val name: String = "")

/**
 * Looks up a named commander's art on Scryfall (https://scryfall.com/docs/api),
 * and tile cards from the other games' databases ([TileCardGame]):
 * only the name the player typed is sent. Results, misses included, are kept
 * in [cacheDir] so each name is asked for once. Requests are spaced out as
 * Scryfall asks and use the phone's default network, like firmware updates.
 */
class ScryfallCommanderArt(
    private val cacheDir: File,
    private val userAgent: String,
    private val baseUrl: String = "https://api.scryfall.com",
    private val pokemonUrl: String = "https://api.tcgdex.net/v2/en",
    private val lorcanaUrl: String = "https://api.lorcast.com/v0",
    private val yugiohUrl: String = "https://db.ygoprodeck.com/api/v7",
    private val opener: HttpConnectionOpener = HttpConnectionOpener.Default,
    private val ioDispatcher: CoroutineDispatcher = Dispatchers.IO,
) {
    private val gate = Mutex()
    private val http = ScryfallHttp(userAgent, opener)

    /**
     * The art for [name] (the [printing] the player picked, else Scryfall's default),
     * from the cache or Scryfall; null when no card matches or the lookup fails.
     */
    suspend fun art(name: String, printing: String? = null): CommanderArt? = withContext(ioDispatcher) {
        val game = TileCardGame.of(printing)
        val id = game.id(printing)
        val key = cacheKey(if (id == null) name else "$name#$id")
        cached(key)?.takeIf { it.metadataCurrent }?.let { return@withContext it.takeIf { art -> art.jpeg.isNotEmpty() } }
        gate.withLock {
            val old = cached(key)
            old?.takeIf { it.metadataCurrent }?.let { return@withLock it.takeIf { art -> art.jpeg.isNotEmpty() } }
            val card = runCatching { if (game == TileCardGame.MAGIC) lookup(name, id) else otherCard(game, name, id ?: throw ScryfallHttp.NotFound()) }.getOrElse { error ->
                // A card Scryfall can't find is remembered; a network failure is tried again later.
                if (error is ScryfallHttp.NotFound) remember(key, CommanderArt(name, "", ByteArray(0)))
                return@withLock old?.takeIf { it.jpeg.isNotEmpty() }
            }
            delay(ScryfallHttp.REQUEST_SPACING_MS)
            val art = runCatching { CommanderArt(card.name, card.artist, old?.jpeg?.takeIf { it.isNotEmpty() } ?: http.fetch(card.artCrop, MAX_ART_BYTES), card.rarity) }.getOrNull()
                ?: return@withLock null
            remember(key, art)
            delay(ScryfallHttp.REQUEST_SPACING_MS)
            art
        }
    }

    private class Card(val name: String, val artist: String, val artCrop: String, val rarity: String?)

    /** Card names starting like [typed], for the commander picker; empty under two letters or offline. */
    suspend fun suggest(typed: String): List<String> = withContext(ioDispatcher) {
        val query = typed.trim()
        if (query.length < 2) return@withContext emptyList()
        gate.withLock {
            val names = runCatching {
                val json = JSONObject(http.fetch("$baseUrl/cards/autocomplete?q=" + URLEncoder.encode(query, "UTF-8"), MAX_JSON_BYTES)
                    .toString(Charsets.UTF_8)).optJSONArray("data")
                (0 until (json?.length() ?: 0)).map { json!!.getString(it) }
            }.getOrDefault(emptyList())
            delay(ScryfallHttp.REQUEST_SPACING_MS)
            names
        }
    }

    /** Each distinct art [name] was printed with, newest first; empty when unknown or offline. */
    suspend fun printings(name: String): List<CommanderPrinting> = withContext(ioDispatcher) {
        gate.withLock {
            val found = runCatching {
                val query = "!\"" + name.trim().replace("\"", "") + "\" game:paper"
                val url = "$baseUrl/cards/search?unique=art&order=released&dir=desc&q=" + URLEncoder.encode(query, "UTF-8")
                val data = JSONObject(http.fetch(url, MAX_LIST_BYTES).toString(Charsets.UTF_8)).optJSONArray("data")
                (0 until (data?.length() ?: 0)).mapNotNull { i ->
                    val card = data!!.getJSONObject(i)
                    val face = frontFace(card) ?: return@mapNotNull null
                    val crop = face.optJSONObject("image_uris")?.optString("art_crop").orEmpty()
                    val id = card.optString("id")
                    if (!crop.startsWith("https://") || !id.matches(PRINTING_ID)) null
                    else CommanderPrinting(id, card.optString("set_name"), face.optString("artist", card.optString("artist")), crop)
                }.take(MAX_PRINTINGS)
            }.getOrDefault(emptyList())
            delay(ScryfallHttp.REQUEST_SPACING_MS)
            found
        }
    }

    /** A random card of [game] for a tile; null offline. Nothing the player typed is sent. */
    suspend fun random(game: TileCardGame = TileCardGame.MAGIC): CommanderPrinting? = withContext(ioDispatcher) {
        gate.withLock {
            val found = runCatching {
                when (game) {
                    TileCardGame.MAGIC -> {
                        val query = "game:paper -type:token"
                        val card = json("$baseUrl/cards/random?q=" + URLEncoder.encode(query, "UTF-8"))
                        val id = card.optString("id")
                        val crop = frontFace(card)?.optJSONObject("image_uris")?.optString("art_crop").orEmpty()
                        if (card.optString("name").isBlank() || !id.matches(PRINTING_ID) || !crop.startsWith("https://")) null
                        else CommanderPrinting(id, card.optString("set_name"), frontFace(card)?.optString("artist").orEmpty(), crop, card.optString("name"))
                    }
                    TileCardGame.POKEMON -> TileCardJson.pokemon(json("$pokemonUrl/random/card"))
                    // Lorcast has no random card: a random set and ink is a small search to pick from.
                    TileCardGame.LORCANA -> TileCardJson.lorcanaCards(text("$lorcanaUrl/cards/search?q=" +
                        URLEncoder.encode("set:${(1..LORCANA_SETS).random()} ink:${LORCANA_INKS.random()}", "UTF-8"))).randomOrNull()
                    TileCardGame.YUGIOH -> TileCardJson.yugiohCards(text("$yugiohUrl/cardinfo.php?num=1&offset=0&sort=random&cachebust")).firstOrNull()
                }
            }.getOrNull()
            delay(ScryfallHttp.REQUEST_SPACING_MS)
            found?.takeIf { it.name.isNotBlank() }
        }
    }

    /** Cards of another game whose names contain [typed], each artwork its own choice; empty under three letters or offline. */
    suspend fun search(game: TileCardGame, typed: String): List<CommanderPrinting> = withContext(ioDispatcher) {
        val query = typed.trim()
        if (game == TileCardGame.MAGIC || query.length < 3) return@withContext emptyList()
        val q = URLEncoder.encode(query, "UTF-8")
        gate.withLock {
            val found = runCatching {
                when (game) {
                    TileCardGame.POKEMON -> TileCardJson.pokemonCards(text("$pokemonUrl/cards?name=like:$q&pagination:page=1&pagination:itemsPerPage=$MAX_PRINTINGS"))
                    TileCardGame.LORCANA -> TileCardJson.lorcanaCards(text("$lorcanaUrl/cards/search?q=$q"))
                    TileCardGame.YUGIOH -> TileCardJson.yugiohCards(text("$yugiohUrl/cardinfo.php?fname=$q&num=$MAX_PRINTINGS&offset=0"))
                    TileCardGame.MAGIC -> emptyList()
                }.filter { it.name.isNotBlank() }.take(MAX_PRINTINGS)
            }.getOrDefault(emptyList())
            delay(ScryfallHttp.REQUEST_SPACING_MS)
            found
        }
    }

    /** Another game's card by its database [id], with the image to show and the artist to credit. */
    private fun otherCard(game: TileCardGame, name: String, id: String): Card {
        val printing = when (game) {
            TileCardGame.POKEMON -> TileCardJson.pokemon(json("$pokemonUrl/cards/" + URLEncoder.encode(id, "UTF-8")))
            TileCardGame.LORCANA -> TileCardJson.lorcana(json("$lorcanaUrl/cards/$id"))
            // YGOPRODeck's art crops are named by id, so no lookup is needed.
            TileCardGame.YUGIOH -> CommanderPrinting(game.prefix + id, "", TileCardJson.YUGIOH_CREDIT, "$YUGIOH_IMAGES/$id.jpg", name)
            TileCardGame.MAGIC -> null
        } ?: throw ScryfallHttp.NotFound()
        return Card(printing.name.ifBlank { name }, printing.artist, printing.artCrop, null)
    }

    private fun text(url: String): String = http.fetch(url, MAX_LIST_BYTES).toString(Charsets.UTF_8)
    private fun json(url: String): JSONObject = JSONObject(http.fetch(url, MAX_JSON_BYTES).toString(Charsets.UTF_8))

    /**
     * A Death May Die investigator's picture for their tile, from the community wiki
     * ([com.turnhub.android.domain.DeathMayDie.artUrl]), cached like tile art; null for an
     * investigator the wiki has no picture of, or offline.
     */
    suspend fun investigator(name: String): ByteArray? = withContext(ioDispatcher) {
        val url = com.turnhub.android.domain.DeathMayDie.artUrl(name) ?: return@withContext null
        val file = File(cacheDir, cacheKey("investigator#$name") + ".img")
        if (file.exists()) return@withContext file.readBytes()
        runCatching { http.fetch(url, MAX_ART_BYTES) }.getOrNull()?.also { image ->
            runCatching { cacheDir.mkdirs(); file.writeBytes(image) }
        }
    }

    /** A printing's art crop for the picker, cached like tile art; null offline. */
    suspend fun preview(printing: CommanderPrinting): ByteArray? = withContext(ioDispatcher) {
        val file = File(cacheDir, cacheKey("preview#${printing.id}") + ".jpg")
        if (file.exists()) return@withContext file.readBytes()
        // Card images come from Scryfall's image host, which asks for no request spacing.
        runCatching { http.fetch(printing.artCrop, MAX_ART_BYTES) }.getOrNull()?.also { jpeg ->
            runCatching { cacheDir.mkdirs(); file.writeBytes(jpeg) }
        }
    }

    private fun lookup(name: String, printing: String?): Card {
        val url = if (printing != null) "$baseUrl/cards/$printing"
            else "$baseUrl/cards/named?fuzzy=" + URLEncoder.encode(name.trim(), "UTF-8")
        val json = JSONObject(http.fetch(url, MAX_JSON_BYTES).toString(Charsets.UTF_8))
        val face = frontFace(json) ?: throw ScryfallHttp.NotFound()
        val crop = face.optJSONObject("image_uris")?.optString("art_crop").orEmpty()
        if (!crop.startsWith("https://")) throw ScryfallHttp.NotFound()
        return Card(json.optString("name", name), face.optString("artist", json.optString("artist")), crop, json.optString("rarity").takeIf { it.isNotBlank() })
    }

    /** Double-faced cards keep their art on each face; the front face is the commander. */
    private fun frontFace(card: JSONObject): JSONObject? =
        card.optJSONObject("image_uris")?.let { card } ?: card.optJSONArray("card_faces")?.optJSONObject(0)

    private fun cached(key: String): CommanderArt? = runCatching {
        val meta = File(cacheDir, "$key.json").takeIf { it.exists() } ?: return null
        val json = JSONObject(meta.readText())
        val image = File(cacheDir, "$key.jpg")
        CommanderArt(json.getString("name"), json.getString("artist"), if (image.exists()) image.readBytes() else ByteArray(0), json.optString("rarity").takeIf { it.isNotBlank() && it != "null" }, json.has("rarity"))
    }.getOrNull()

    private fun remember(key: String, art: CommanderArt) {
        runCatching {
            cacheDir.mkdirs()
            if (art.jpeg.isNotEmpty()) File(cacheDir, "$key.jpg").writeBytes(art.jpeg)
            File(cacheDir, "$key.json").writeText(JSONObject().put("name", art.cardName).put("artist", art.artist).put("rarity", art.rarity ?: JSONObject.NULL).toString())
        }
    }

    companion object {
        private const val MAX_JSON_BYTES = 256 * 1024
        private const val MAX_ART_BYTES = 2 * 1024 * 1024
        private const val MAX_LIST_BYTES = 4 * 1024 * 1024
        private const val MAX_PRINTINGS = 30
        private const val YUGIOH_IMAGES = "https://images.ygoprodeck.com/images/cards_cropped"
        /** Lorcana's main sets so far, and its inks, for a random card. */
        private const val LORCANA_SETS = 10
        private val LORCANA_INKS = listOf("amber", "amethyst", "emerald", "ruby", "sapphire", "steel")
        private val PRINTING_ID = com.turnhub.android.standalone.StandaloneGame.PRINTING_ID

        /** Same name, same file, however it was capitalised or spaced. */
        fun cacheKey(name: String): String = MessageDigest.getInstance("SHA-256")
            .digest(name.trim().lowercase().replace(Regex("\\s+"), " ").toByteArray())
            .take(16).joinToString("") { "%02x".format(it) }
    }
}
