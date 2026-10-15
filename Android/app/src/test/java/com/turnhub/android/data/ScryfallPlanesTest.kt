package com.turnhub.android.data

import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder
import java.io.ByteArrayInputStream
import java.io.InputStream
import java.net.HttpURLConnection
import java.net.URL

class ScryfallPlanesTest {
    @get:Rule val folder = TemporaryFolder()

    private class Reply(val code: Int, val body: ByteArray)

    private class FakeWeb(val replies: (String) -> Reply) {
        val asked = mutableListOf<String>()
        val opener = HttpConnectionOpener { url ->
            asked += url.toString()
            object : HttpURLConnection(url) {
                private val reply = replies(url.toString())
                override fun getResponseCode() = reply.code
                override fun getInputStream(): InputStream = ByteArrayInputStream(reply.body)
                override fun setRequestProperty(key: String, value: String) = Unit
                override fun connect() = Unit
                override fun disconnect() = Unit
                override fun usingProxy() = false
            }
        }
    }

    private fun card(id: String, name: String, type: String) =
        """{"id":"$id","name":"$name","type_line":"$type","oracle_text":"Whenever chaos ensues, draw a card.","artist":"A. Painter",
            "set_name":"Planechase","image_uris":{"art_crop":"https://cards.scryfall.io/art_crop/$id.jpg"}}"""

    private fun planes(web: FakeWeb) = ScryfallPlanes(folder.newFolder("data"), folder.newFolder("art"), "TurnHub/test",
        opener = web.opener, ioDispatcher = Dispatchers.Unconfined)

    @Test fun `downloads every page, keeps the list and tells planes from phenomena`() = runBlocking {
        val web = FakeWeb { url ->
            if (url.contains("page=2")) Reply(200, """{"has_more":false,"data":[${card("b", "Spatial Merging", "Phenomenon")}]}""".toByteArray())
            else Reply(200, """{"has_more":true,"next_page":"https://api.scryfall.com/cards/search?page=2",
                "data":[${card("a", "Academy at Tolaria West", "Plane — Dominaria")},{"id":"no-art","name":"x","type_line":"Plane"}]}""".toByteArray())
        }
        val source = planes(web)
        assertNull(source.saved())
        val cards = source.download()
        assertEquals(listOf("a", "b"), cards.map { it.id })
        assertEquals(listOf(false, true), cards.map { it.phenomenon })
        assertEquals("A. Painter", cards.first().artist)
        assertTrue(web.asked.first().startsWith("https://api.scryfall.com/cards/search?unique=cards&order=name&q="))
        assertEquals(cards, source.saved())
    }

    @Test fun `a failed download keeps the saved list`() = runBlocking {
        var up = true
        val web = FakeWeb { if (up) Reply(200, """{"has_more":false,"data":[${card("a", "Bant", "Plane — Alara")}]}""".toByteArray()) else Reply(503, ByteArray(0)) }
        val source = planes(web)
        source.download()
        up = false
        assertTrue(runCatching { source.download() }.isFailure)
        assertEquals(listOf("a"), source.saved()?.map { it.id })
    }

    @Test fun `art is fetched once and then comes from the cache`() = runBlocking {
        val jpeg = byteArrayOf(1, 2, 3)
        val web = FakeWeb { Reply(200, jpeg) }
        val source = planes(web)
        val plane = PlaneInfo("a", "Bant", "Plane — Alara", "", "A. Painter", "https://cards.scryfall.io/art_crop/a.jpg", "Planechase")
        assertArrayEquals(jpeg, source.art(plane))
        assertArrayEquals(jpeg, source.art(plane))
        assertEquals(1, web.asked.size)
    }
}
