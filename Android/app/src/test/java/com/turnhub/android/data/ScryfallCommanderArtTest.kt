package com.turnhub.android.data

import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder
import java.io.ByteArrayInputStream
import java.io.InputStream
import java.net.HttpURLConnection
import java.net.URL

class ScryfallCommanderArtTest {
    @get:Rule val folder = TemporaryFolder()

    private class Reply(val code: Int, val body: ByteArray)

    /** Answers by URL and records what was asked, including the headers. */
    private class FakeWeb(val replies: (String) -> Reply) {
        val asked = mutableListOf<String>()
        val agents = mutableListOf<String?>()
        val opener = HttpConnectionOpener { url ->
            asked += url.toString()
            object : HttpURLConnection(url) {
                private val reply = replies(url.toString())
                override fun getResponseCode() = reply.code
                override fun getInputStream(): InputStream = ByteArrayInputStream(reply.body)
                override fun setRequestProperty(key: String, value: String) { if (key == "User-Agent") agents += value }
                override fun connect() = Unit
                override fun disconnect() = Unit
                override fun usingProxy() = false
            }
        }
    }

    private val jpeg = byteArrayOf(1, 2, 3)

    private fun card(json: String) = Reply(200, json.toByteArray())

    @Test fun `finds the art crop, credits the artist and asks only once`() = runBlocking {
        val web = FakeWeb { url ->
            if (url.contains("/cards/named")) card("""{"name":"Hakbal of the Surging Soul","artist":"A. Painter",
                "image_uris":{"art_crop":"https://cards.scryfall.io/art_crop/x.jpg"}}""") else Reply(200, jpeg)
        }
        val art = ScryfallCommanderArt(folder.root, "TurnHub/test", opener = web.opener, ioDispatcher = Dispatchers.Unconfined)
        val found = art.art("hakbal")!!
        assertEquals("Hakbal of the Surging Soul", found.cardName)
        assertEquals("A. Painter", found.artist)
        assertArrayEquals(jpeg, found.jpeg)
        assertEquals("https://api.scryfall.com/cards/named?fuzzy=hakbal", web.asked.first())
        assertEquals(listOf("TurnHub/test", "TurnHub/test"), web.agents)
        // The same name, spaced or capitalised differently, comes from the cache.
        assertArrayEquals(jpeg, art.art("  HAKBAL ")!!.jpeg)
        assertEquals(2, web.asked.size)
    }

    @Test fun `a double-faced card uses its front face`() = runBlocking {
        val web = FakeWeb { url ->
            if (url.contains("/cards/named")) card("""{"name":"Front // Back","card_faces":[
                {"artist":"Front Artist","image_uris":{"art_crop":"https://cards.scryfall.io/front.jpg"}},
                {"artist":"Back Artist","image_uris":{"art_crop":"https://cards.scryfall.io/back.jpg"}}]}""") else Reply(200, jpeg)
        }
        val found = ScryfallCommanderArt(folder.root, "t", opener = web.opener, ioDispatcher = Dispatchers.Unconfined).art("front")!!
        assertEquals("Front Artist", found.artist)
        assertEquals("https://cards.scryfall.io/front.jpg", web.asked.last())
    }

    @Test fun `an unknown card is remembered as a miss`() = runBlocking {
        val web = FakeWeb { Reply(404, "{}".toByteArray()) }
        val art = ScryfallCommanderArt(folder.root, "t", opener = web.opener, ioDispatcher = Dispatchers.Unconfined)
        assertNull(art.art("Not A Card"))
        assertNull(art.art("not a card"))
        assertEquals(1, web.asked.size)
    }
}
