package com.turnhub.android.standalone

import com.turnhub.android.data.TabletSeat
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test
import kotlinx.coroutines.test.runTest

class PreviewProfileTransferTest {
    private class Store(library: LocalLibrary = LocalLibrary()) : StandaloneStore {
        var savedGame: String? = null
        var savedLibrary = library.toJson().toString()
        var commits = 0
        var reject = false
        override fun loadGame() = savedGame
        override fun loadRecords(): String? = null
        override fun loadLibrary() = savedLibrary
        override fun saveSnapshot(game: String, library: String): Boolean {
            if (reject) return false
            savedGame = game; savedLibrary = library; return true
        }
        override fun commitSnapshot(game: String, library: String): Boolean {
            commits++
            return saveSnapshot(game, library)
        }
    }

    private val owner = SavedLocalPlayer("source-owner", "Same name", "1234ABCD",
        personalization = LocalPersonalization(avatar = "cat"),
        links = listOf(AtlasProfileLink("THA-1", "1234ABCD", "0123456789ABCDEF0123456789ABCDEF")),
        credential = LocalCredential.create("2468"), createdAtMs = 1_700_000_000_000L,
        commanders = listOf("Commander one" to null, "Commander two" to null, "Commander three" to null))
    private val other = SavedLocalPlayer("source-other", "Same name", "8765DCBA")
    private val source get() = LocalLibrary(players = listOf(owner, other), deviceAdmin = owner.localId)
    private fun bundle() = PreviewProfileTransfer.export(source, setOf(owner.localId, other.localId), mapOf(owner.localId to "2468"))

    @Test fun `only the production app and an explicit Preview result request can export`() {
        assertTrue(PreviewProfileTransfer.allowedCaller(PreviewProfileTransfer.TURNHUB, PreviewProfileTransfer.PREVIEW, PreviewProfileTransfer.ACTION))
        assertFalse(PreviewProfileTransfer.allowedCaller(PreviewProfileTransfer.TURNHUB, null, PreviewProfileTransfer.ACTION))
        assertFalse(PreviewProfileTransfer.allowedCaller(PreviewProfileTransfer.TURNHUB, "another.app", PreviewProfileTransfer.ACTION))
        assertFalse(PreviewProfileTransfer.allowedCaller(PreviewProfileTransfer.PREVIEW, PreviewProfileTransfer.PREVIEW, PreviewProfileTransfer.ACTION))
        assertFalse(PreviewProfileTransfer.allowedCaller(PreviewProfileTransfer.TURNHUB, PreviewProfileTransfer.PREVIEW, "wrong"))
    }

    @Test fun `every selected protected profile needs its PIN and only selected identities are copied`() {
        assertThrows(IllegalArgumentException::class.java) { PreviewProfileTransfer.export(source, setOf(owner.localId), emptyMap()) }
        assertThrows(IllegalArgumentException::class.java) { PreviewProfileTransfer.export(source, setOf(owner.localId), mapOf(owner.localId to "0000")) }
        val onlyOther = PreviewProfileTransfer.export(source, setOf(other.localId), emptyMap())
        assertEquals(listOf(other.profileId), PreviewProfileTransfer.accounts(onlyOther).map { it.getString("profileId") })
        assertTrue(onlyOther.isNull("adminProfileId"))
        assertEquals(2, PreviewProfileTransfer.accounts(bundle()).size)
    }

    @Test fun `batch keeps identity age credentials appearance commanders and links in one durable commit`() {
        val store = Store()
        val table = StandaloneTable(store)
        val game = table.state.value.game
        assertEquals(StandaloneTable.CopyResult.ADDED, table.importPreviewProfiles(bundle()))
        assertEquals(1, store.commits)
        assertSame(game, table.state.value.game)
        val library = table.state.value.library
        val copied = library.players.first { it.profileId == owner.profileId }
        assertNotEquals(owner.localId, copied.localId)
        assertEquals(owner.createdAtMs, copied.createdAtMs)
        assertEquals(owner.personalization, copied.personalization)
        assertEquals(owner.commanders, copied.commanders)
        assertEquals(owner.links, copied.links)
        assertTrue(copied.credential!!.verifies("2468"))
        assertEquals(copied.localId, library.deviceAdmin)
        assertEquals(2, library.players.size) // Equal labels never merge identities.
        assertEquals(StandaloneTable.CopyResult.ALREADY_HERE, table.importPreviewProfiles(bundle()))
        assertEquals(1, store.commits)
        assertEquals(library, StandaloneTable(store).state.value.library)
    }

    @Test fun `existing destination settings and Admin stay in place and invalid batches never partially save`() {
        val existing = owner.copy(localId = "preview-owner", name = "Preview name", commanders = emptyList())
        val local = LocalLibrary(players = listOf(existing), deviceAdmin = existing.localId)
        val store = Store(local)
        val table = StandaloneTable(store)
        val malformed = bundle().apply { getJSONArray("accounts").getJSONObject(1).put("profileId", "invalid") }
        assertEquals(StandaloneTable.CopyResult.NOT_SAVED, table.importPreviewProfiles(malformed))
        assertEquals(local, table.state.value.library)
        assertEquals(0, store.commits)
        assertEquals(StandaloneTable.CopyResult.ADDED, table.importPreviewProfiles(bundle()))
        assertEquals(existing, table.state.value.library.players.first { it.profileId == owner.profileId })
        assertEquals(existing.localId, table.state.value.library.deviceAdmin)
    }

    @Test fun `failed durable write keeps the original destination library and on disk data`() {
        val store = Store().apply { reject = true }
        val original = store.savedLibrary
        val table = StandaloneTable(store)
        assertEquals(StandaloneTable.CopyResult.NOT_SAVED, table.importPreviewProfiles(bundle()))
        assertTrue(table.state.value.library.players.isEmpty())
        assertEquals(original, store.savedLibrary)
    }

    @Test fun `history and original match identities survive import without duplicate statistics`() = runTest {
        var now = 1_700_000_000_000L
        val phone = StandaloneTable(Store(), wallClock = { now }, shortGameMs = 0)
        phone.createAccount("Player")
        val player = phone.state.value.library.players.single()
        phone.selectPlayer(player.localId); phone.addPlayer("Opponent")
        phone.chooseStarter(0); phone.start(); now += 120_000
        assertTrue(phone.control(TabletSeat(StandaloneGame.seatHandle(0).id, 1), "win"))
        val copy = PreviewProfileTransfer.export(phone.state.value.library, setOf(player.localId), emptyMap())
        val destination = StandaloneTable(Store())
        assertEquals(StandaloneTable.CopyResult.ADDED, destination.importPreviewProfiles(copy))
        assertEquals(StandaloneTable.CopyResult.ALREADY_HERE, destination.importPreviewProfiles(copy))
        val imported = destination.state.value.library
        assertEquals(phone.state.value.library.history.single().recordId, imported.history.single().recordId)
        assertEquals(LocalTotals(1, 1, 0), imported.totals(imported.players.single().localId))
    }

    @Test fun `oversized payloads unsupported versions and duplicate identities are refused`() {
        assertThrows(IllegalArgumentException::class.java) { PreviewProfileTransfer.read("{}   ".byteInputStream(), 2) }
        assertEquals("{}", PreviewProfileTransfer.read("{}".byteInputStream(), 2).toString())
        assertThrows(IllegalArgumentException::class.java) { PreviewProfileTransfer.accounts(bundle().put("transferSchema", 2)) }
        assertThrows(IllegalArgumentException::class.java) {
            PreviewProfileTransfer.accounts(bundle().apply { getJSONArray("accounts").put(getJSONArray("accounts").getJSONObject(0)) })
        }
    }
}
