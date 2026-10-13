package com.turnhub.android.standalone

import com.turnhub.android.protocol.TableState
import kotlinx.coroutines.test.runTest
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test
import java.util.Base64
import kotlin.random.Random

class LocalAccountsTest {
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
