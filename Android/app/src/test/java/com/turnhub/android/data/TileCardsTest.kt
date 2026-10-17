package com.turnhub.android.data

import com.turnhub.android.protocol.GameProfile
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class TileCardsTest {
    @Test fun `saved ids say which game's card they are and are checked`() {
        assertEquals(TileCardGame.MAGIC, TileCardGame.of("0000579f-7b35-4ed3-b44c-db2a538066fe"))
        assertEquals(TileCardGame.MAGIC, TileCardGame.of(null))
        assertEquals(TileCardGame.POKEMON, TileCardGame.of("pokemon:sv03.5-001"))
        assertTrue(TileCardGame.valid("pokemon:sv03.5-001"))
        assertTrue(TileCardGame.valid("lorcana:crd_a6f3c215973844fc8ae5cbc2a4ea388f"))
        assertTrue(TileCardGame.valid("yugioh:46986414"))
        assertFalse(TileCardGame.valid("yugioh:../46986414"))
        assertFalse(TileCardGame.valid("lorcana:crd_x"))
        assertFalse(TileCardGame.valid("not-a-uuid"))
        assertEquals(TileCardGame.LORCANA, TileCardGame.forProfile(GameProfile.LORCANA))
        assertEquals(TileCardGame.MAGIC, TileCardGame.forProfile(GameProfile.GENERIC))
    }

    @Test fun `reads each database's cards`() {
        val pokemon = TileCardJson.pokemonCards("""[{"id":"base1-58","localId":"58","name":"Pikachu","image":"https://assets.tcgdex.net/en/base/base1/58"},
            {"id":"x-1","name":"No image"}]""").single()
        assertEquals("pokemon:base1-58", pokemon.id)
        assertEquals("https://assets.tcgdex.net/en/base/base1/58/high.jpg", pokemon.artCrop)
        assertEquals("Pikachu", pokemon.name)
        assertEquals("The Pokémon Company Art Team", TileCardJson.pokemon(org.json.JSONObject("""{"id":"swsh-1","name":"P",
            "image":"https://assets.tcgdex.net/en/x/1","illustrator":"Illus. & Direc. The Pokémon Company Art Team"}"""))!!.artist)

        val elsa = TileCardJson.lorcanaCards("""{"results":[{"id":"crd_a6f3c215973844fc8ae5cbc2a4ea388f","name":"Elsa","version":"Concerned Sister",
            "image_uris":{"digital":{"normal":"https://cards.lorcast.io/card/digital/normal/crd_a6f3c215973844fc8ae5cbc2a4ea388f.avif"}},
            "illustrators":["Hollie Hibbert"],"set":{"name":"Winterspell"}}]}""").single()
        assertEquals("Elsa - Concerned Sister", elsa.name)
        assertEquals("Hollie Hibbert", elsa.artist)
        assertEquals("Winterspell", elsa.setName)

        val magician = TileCardJson.yugiohCards("""{"data":[{"id":46986414,"name":"Dark Magician","type":"Normal Monster","card_images":[
            {"id":46986414,"image_url_cropped":"https://images.ygoprodeck.com/images/cards_cropped/46986414.jpg"},
            {"id":38033121,"image_url_cropped":"https://images.ygoprodeck.com/images/cards_cropped/38033121.jpg"}]}]}""")
        assertEquals(listOf("yugioh:46986414", "yugioh:38033121"), magician.map { it.id })
        assertEquals(TileCardJson.YUGIOH_CREDIT, magician[0].artist)
        assertNull(TileCardJson.lorcana(org.json.JSONObject("""{"id":"crd_bad","name":"x"}""")))
    }
}
