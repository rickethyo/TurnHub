package com.turnhub.android.data

import com.turnhub.android.protocol.GameProfile
import org.json.JSONObject

/** The part of a full card image that is its art, as fractions of the card's width and height. */
class ArtCrop(val left: Float, val top: Float, val right: Float, val bottom: Float)

/**
 * The card games a player can take tile art from, in any game: a Magic card
 * (Scryfall, whose printing ids are bare UUIDs) or a card from another game's
 * community database, saved as `<prefix><that database's id>`. Commander names are independent of the chosen artwork.
 */
enum class TileCardGame(val label: String, val prefix: String, private val idPattern: Regex, val crop: ArtCrop?, val source: String) {
    MAGIC("Magic", "", com.turnhub.android.standalone.StandaloneGame.PRINTING_ID, null, "Scryfall"),
    /** TCGdex (https://tcgdex.dev): full card images, so the art window is cut out. */
    POKEMON("Pokémon", "pokemon:", Regex("[A-Za-z0-9][A-Za-z0-9.\\-]{0,39}"), ArtCrop(.08f, .11f, .92f, .47f), "TCGdex"),
    /** Lorcast (https://lorcast.com/docs/api): full card images (AVIF); the art is the card's top half. */
    LORCANA("Lorcana", "lorcana:", Regex("crd_[0-9a-f]{32}"), ArtCrop(0f, .02f, 1f, .55f), "Lorcast"),
    /** YGOPRODeck (https://ygoprodeck.com/api-guide/): its own art crops. */
    YUGIOH("Yu-Gi-Oh!", "yugioh:", Regex("[0-9]{1,10}"), null, "YGOPRODeck");

    /** The database's own id in [printing], when it is a valid id for this game. */
    fun id(printing: String?): String? = printing?.takeIf { it.startsWith(prefix) }?.removePrefix(prefix)?.takeIf { it.matches(idPattern) }

    companion object {
        /** The game a saved printing belongs to; Magic for bare Scryfall ids and none. */
        fun of(printing: String?): TileCardGame =
            entries.firstOrNull { it.prefix.isNotEmpty() && printing?.startsWith(it.prefix) == true } ?: MAGIC

        const val NO_ART = "none"
        fun valid(printing: String): Boolean = printing == NO_ART || of(printing).id(printing) != null

        /** The picker opens on the game being played, when it has a card database. */
        fun forProfile(profile: GameProfile): TileCardGame = when (profile) {
            GameProfile.POKEMON -> POKEMON
            GameProfile.LORCANA -> LORCANA
            GameProfile.YUGIOH -> YUGIOH
            else -> MAGIC
        }
    }
}

/** Reads the other games' card databases; pure, so it is tested without a network. */
internal object TileCardJson {
    fun pokemonCards(body: String): List<CommanderPrinting> {
        val list = org.json.JSONArray(body)
        return (0 until list.length()).mapNotNull { pokemon(list.getJSONObject(it)) }
    }

    /** One TCGdex card (brief or full); cards without an image are skipped. */
    fun pokemon(card: JSONObject): CommanderPrinting? {
        val id = card.optString("id")
        val image = card.optString("image")
        if (TileCardGame.POKEMON.id(TileCardGame.POKEMON.prefix + id) == null || !image.startsWith("https://")) return null
        return CommanderPrinting(TileCardGame.POKEMON.prefix + id, card.optJSONObject("set")?.optString("name").orEmpty(),
            // "Illus. & Direc. The Pokémon Company Art Team": keep who, not the role, so a short credit still says it.
            card.optString("illustrator").replace(Regex("^Illus\\.\\s*&\\s*Direc\\.\\s*"), ""), "$image/high.jpg", card.optString("name"))
    }

    fun lorcanaCards(body: String): List<CommanderPrinting> {
        val list = JSONObject(body).optJSONArray("results") ?: return emptyList()
        return (0 until list.length()).mapNotNull { lorcana(list.getJSONObject(it)) }
    }

    /** One Lorcast card, named the way the card prints it: "Elsa - Spirit of Winter". */
    fun lorcana(card: JSONObject): CommanderPrinting? {
        val id = card.optString("id")
        val image = card.optJSONObject("image_uris")?.optJSONObject("digital")?.optString("normal").orEmpty()
        if (TileCardGame.LORCANA.id(TileCardGame.LORCANA.prefix + id) == null || !image.startsWith("https://")) return null
        val name = listOf(card.optString("name"), card.optString("version")).filter { it.isNotBlank() && it != "null" }.joinToString(" - ")
        val artists = card.optJSONArray("illustrators")?.let { a -> (0 until a.length()).map { a.getString(it) } }.orEmpty()
        return CommanderPrinting(TileCardGame.LORCANA.prefix + id, card.optJSONObject("set")?.optString("name").orEmpty(),
            artists.joinToString(" & "), image, name)
    }

    /** YGOPRODeck: each of a card's artworks is its own choice; the art belongs to Konami. */
    fun yugiohCards(body: String): List<CommanderPrinting> {
        val list = JSONObject(body).optJSONArray("data") ?: return emptyList()
        return (0 until list.length()).flatMap { i ->
            val card = list.getJSONObject(i)
            val images = card.optJSONArray("card_images") ?: return@flatMap emptyList()
            (0 until images.length()).mapNotNull { j ->
                val image = images.getJSONObject(j)
                val id = image.optLong("id").toString()
                val crop = image.optString("image_url_cropped")
                if (TileCardGame.YUGIOH.id(TileCardGame.YUGIOH.prefix + id) == null || !crop.startsWith("https://")) null
                else CommanderPrinting(TileCardGame.YUGIOH.prefix + id, card.optString("type"), YUGIOH_CREDIT, crop, card.optString("name"))
            }
        }
    }

    const val YUGIOH_CREDIT = "© Konami"
}
