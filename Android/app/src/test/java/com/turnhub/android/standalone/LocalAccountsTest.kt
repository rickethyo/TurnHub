package com.turnhub.android.standalone

import com.turnhub.android.protocol.TableState
import com.turnhub.android.domain.TileBackground
import com.turnhub.android.domain.TileBackgroundStyle
import kotlinx.coroutines.test.runTest
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test
import java.util.Base64
import kotlin.random.Random

class LocalAccountsTest {
    @Test fun `tile backgrounds persist by identity across swaps and clearing without changing avatars`() {
        val store = Store()
        val table = StandaloneTable(store)
        table.addPlayer("Alex"); table.addPlayer("Alex")
        val players = table.state.value.library.players
        val background = TileBackground(TileBackgroundStyle.IMAGE, "#123ABC", Base64.getEncoder().encodeToString(byteArrayOf(1, 2)))
        val prefs = LocalPersonalization(avatar = "cat", tileBackground = background)
        assertTrue(table.saveAccount(players[0].localId, players[0].name, prefs))
        table.swapPlayers(0, 1)
        val state = table.state.value
        val summary = state.game.toSummary(0, 0, state.library.players.associate { it.localId to it.personalization })
        assertEquals(TileBackground(), summary.players[0].tileBackground)
        assertEquals(background, summary.players[1].tileBackground)
        assertEquals("cat", summary.players[1].avatar?.key)
        table.clearPlayers()
        val restored = StandaloneTable(store)
        restored.selectPlayer(players[0].localId)
        assertEquals(prefs, restored.state.value.library.players[0].personalization)
        assertEquals(players[0].localId, restored.state.value.game.players.single().localId)
        assertTrue(restored.state.value.library.history.isEmpty())
    }

    @Test fun `connected decoration requires verified links and preserves canonical state`() {
        val background = TileBackground(TileBackgroundStyle.SOLID, "#123ABC")
        val local = SavedLocalPlayer("local", "Alex", "AABBCCDD", LocalPersonalization(tileBackground = background))
        val base = StandaloneGame().addPlayer("Alex", profileId = local.profileId).toSummary(0, 0).copy(atlasId = "atlas-a")
        val unlinked = LocalLibrary(players = listOf(local))
        assertEquals(base, unlinked.decorateTable(base))
        val linked = LocalLibrary(players = listOf(local.copy(links = listOf(AtlasProfileLink("atlas-a", local.profileId)))))
        val decorated = linked.decorateTable(base)
        assertEquals(background, decorated.players.single().tileBackground)
        assertEquals(base, decorated.copy(players = decorated.players.map { it.copy(tileBackground = TileBackground()) }))
        val other = base.copy(atlasId = "atlas-b")
        assertEquals(other, linked.decorateTable(other))
        assertNull(linked.linkedPlayer("atlas-a", null))
    }

    @Test fun `background settings reject malformed colors styles and oversized images`() {
        assertEquals(TileBackground(), LocalPersonalization.fromJson(JSONObject()).tileBackground)
        assertTrue(runCatching { TileBackground(TileBackgroundStyle.SOLID, "red") }.isFailure)
        assertTrue(runCatching { TileBackground(TileBackgroundStyle.IMAGE) }.isFailure)
        assertTrue(runCatching { TileBackground(image = "not base64!") }.isFailure)
        assertTrue(runCatching { TileBackground(image = Base64.getEncoder().encodeToString(ByteArray(TileBackground.MAX_IMAGE_BYTES + 1))) }.isFailure)
        assertTrue(runCatching { LocalPersonalization.fromJson(JSONObject("""{"tileBackground":{"style":"unknown"}}""")) }.isFailure)
        val store = Store()
        val table = StandaloneTable(store)
        table.createAccount("Alex")
        val player = table.state.value.library.players.single()
        store.fail = true
        assertFalse(table.saveAccount(player.localId, player.name, player.personalization.copy(tileBackground = TileBackground(TileBackgroundStyle.GRADIENT))))
        assertEquals(player, table.state.value.library.players.single())
        assertNotNull(table.state.value.storageProblem)
    }

    private class Store : StandaloneStore {
        var game: String? = null
        var library: String? = null
        var fail = false
        override fun loadGame() = game
        override fun loadLibrary() = library
        override fun loadRecords(): String? = null
        override fun saveSnapshot(game: String, library: String): Boolean {
            if (fail) return false
            this.game = game; this.library = library; return true
        }
    }

    @Test fun `offline accounts persist personalization and identities without sharing names`() {
        val store = Store()
        val table = StandaloneTable(store)
        assertTrue(table.createAccount("Alex"))
        assertTrue(table.createAccount("Alex"))
        val players = table.state.value.library.players
        assertNotEquals(players[0].profileId, players[1].profileId)
        assertTrue(players.all { LocalProfileIds.valid(it.profileId) })
        val prefs = LocalPersonalization("#123ABC", "cat", Base64.getEncoder().encodeToString(byteArrayOf(1, 2)), true)
        assertTrue(table.saveAccount(players[0].localId, "New name", prefs))
        val restored = StandaloneTable(store).state.value.library
        assertEquals("New name", restored.players[0].name)
        assertEquals(prefs, restored.players[0].personalization)
        assertEquals(players[0].profileId, restored.players[0].profileId)
        assertEquals("Alex", restored.players[1].name)
    }

    @Test fun `legacy UUID history gains stable compatible IDs once without losing results`() {
        val store = Store()
        store.library = """{"schema":1,"players":[{"localId":"old-uuid","name":"Alex"}],"history":[],"deliveries":[]}"""
        val first = StandaloneTable(store).state.value.library.players.single()
        val second = StandaloneTable(store).state.value.library.players.single()
        assertEquals("old-uuid", second.localId)
        assertEquals(first.profileId, second.profileId)
        assertEquals(2, JSONObject(store.library!!).getInt("schema"))
    }

    @Test fun `automatic imports require verified links scoped to the destination not public ID collisions`() = runTest {
        val store = Store()
        var now = 0L
        val table = StandaloneTable(store, wallClock = { now })
        table.addPlayer("Alex"); table.addPlayer("Alex")
        val players = table.state.value.library.players
        table.start(); now = 100_000
        table.control(com.turnhub.android.data.TabletSeat(9, 1), "win")
        val record = table.state.value.library.history.single()
        val available = players.map { it.profileId }.toSet()
        assertNull(table.state.value.library.automaticMapping(record, "atlas-a", available))
        assertTrue(table.linkAccount(players[0].localId, "atlas-a", players[0].profileId))
        assertNull(table.state.value.library.automaticMapping(record, "atlas-a", available))
        assertFalse(table.linkAccount(players[1].localId, "atlas-a", players[0].profileId))
        assertTrue(table.linkAccount(players[1].localId, "atlas-a", players[1].profileId))
        assertNull(table.state.value.library.automaticMapping(record, "atlas-b", available))
        table.queueAutomaticImports("atlas-a", available)
        val delivery = table.state.value.library.delivery(record.recordId)
        assertEquals(DeliveryStatus.PENDING, delivery.status)
        assertEquals(players.map { it.profileId }, delivery.profileIds)
        table.sendRecords("atlas-a") { com.turnhub.android.data.RawResponse(200,
            """{"ok":true,"duplicate":false,"credited":2,"unmatched":[]}""") }
        assertEquals(DeliveryStatus.IMPORTED, table.state.value.library.delivery(record.recordId).status)
        assertEquals(record, StandaloneTable(store).state.value.library.history.single())
        table.queueAutomaticImports("atlas-a", available)
        var requests = 0
        table.sendRecords("atlas-a") { requests++; null }
        assertEquals(0, requests)
    }

    @Test fun `full offline statistics and final counter details survive restart`() {
        val first = object : Random() { override fun nextBits(bitCount: Int) = 0 }
        val game = StandaloneGame().addPlayer("Ana", localId = "a").addPlayer("Ben", localId = "b")
            .start(0, "g", first).pass(0, 10_000).pass(1, 40_000).changeCounter(0, "energy", 5)
            .commanderDamage(0, 1, 1, 4).claimWin(0, 100_000)
        val library = LocalLibrary(players = listOf(SavedLocalPlayer("a", "Ana"), SavedLocalPlayer("b", "Ben")))
            .withRecord(game.record()!!)
        val restored = LocalLibrary.fromJson(library.toJson())
        val stats = restored.profileStatistics("a")
        assertTrue(stats.detailed)
        assertEquals(100_000L, stats.lifetime.totalGameMs)
        assertEquals(10_000L, stats.lifetime.totalTurnMs)
        assertEquals("Win", stats.lastGame.result)
        assertEquals(10_000L, stats.lastGame.fastestTurnMs)
        assertEquals(5, restored.history.single().players[0].counters.energy)
        assertEquals(listOf(4, 0), restored.history.single().players[0].commanderBySource[1])
        assertEquals(library.history, restored.history)
    }

    @Test fun `failed account or link persistence leaves the prior facts untouched`() {
        val store = Store()
        val table = StandaloneTable(store)
        table.createAccount("Ana")
        val before = table.state.value.library
        store.fail = true
        assertFalse(table.linkAccount(before.players[0].localId, "atlas", "AABBCCDD"))
        assertEquals(before, table.state.value.library)
        assertNotNull(table.state.value.storageProblem)
    }

    @Test fun `local account credentials preserve Atlas rules without storing plaintext and lock settings after restart`() {
        val store = Store()
        val table = StandaloneTable(store)
        assertTrue(table.createAccount("Ana", LocalCredential.create("1234")))
        val player = table.state.value.library.players.single()
        assertFalse(store.library!!.contains("\"1234\""))
        assertTrue(player.credential!!.verifies("1234"))
        assertFalse(player.credential.verifies("9999"))
        val restored = StandaloneTable(store)
        assertFalse(restored.accountUnlocked(player.localId))
        assertFalse(restored.saveAccount(player.localId, "Changed", LocalPersonalization()))
        assertFalse(restored.unlockAccount(player.localId, "9999"))
        assertTrue(restored.unlockAccount(player.localId, "1234"))
        assertTrue(restored.saveAccount(player.localId, "Changed", LocalPersonalization()))
        assertTrue(runCatching { LocalCredential.create("123") }.isFailure)
    }
}
