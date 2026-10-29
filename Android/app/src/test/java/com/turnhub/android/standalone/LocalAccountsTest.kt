package com.turnhub.android.standalone

import com.turnhub.android.protocol.TableState
import com.turnhub.android.domain.TileBackground
import com.turnhub.android.domain.TileBackgroundStyle
import com.turnhub.android.ui.tablet.starSeats
import kotlinx.coroutines.test.runTest
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test
import java.util.Base64
import kotlin.random.Random

class LocalAccountsTest {
    @Test fun `tile backgrounds persist by identity across swaps and clearing without changing avatars`() {
        val store = Store()
        val table = StandaloneTable(store, shortGameMs = 0)
        table.addPlayer("Alex"); table.addPlayer("Alex")
        val players = table.state.value.library.players
        val background = TileBackground(TileBackgroundStyle.IMAGE, "#123ABC", Base64.getEncoder().encodeToString(byteArrayOf(1, 2)))
        val prefs = LocalPersonalization(color = "#26A69A", avatar = "cat", tileBackground = background)
        assertTrue(table.saveAccount(players[0].localId, players[0].name, prefs))
        table.swapPlayers(0, 1)
        val state = table.state.value
        val summary = state.game.toSummary(0, 0, state.library.players.associate { it.localId to it.personalization })
        assertEquals(TileBackground(), summary.players[0].tileBackground)
        assertEquals(background, summary.players[1].tileBackground)
        assertEquals("cat", summary.players[1].avatar?.key)
        assertEquals(prefs.color, summary.players[1].color)
        // Star previews must carry the same personalization as ordinary seats and live panels.
        val starSeat = summary.starSeats()[1]
        assertEquals(background, starSeat.background)
        assertEquals(prefs.color, starSeat.color)
        assertEquals("cat", starSeat.avatar?.key)
        table.clearPlayers()
        val restored = StandaloneTable(store, shortGameMs = 0)
        restored.selectPlayer(players[0].localId)
        assertEquals(prefs, restored.state.value.library.players[0].personalization)
        assertEquals(players[0].localId, restored.state.value.game.players.single().localId)
        assertTrue(restored.state.value.library.history.isEmpty())
    }

    @Test fun `connected decoration requires verified links and preserves canonical state`() {
        val background = TileBackground(TileBackgroundStyle.SOLID, "#123ABC")
        val local = SavedLocalPlayer("local", "Alex", "AABBCCDD", LocalPersonalization(tileBackground = background))
        val localSummary = StandaloneGame().addPlayer("Alex").toSummary(0, 0)
        val base = localSummary.copy(atlasId = "atlas-a", players = localSummary.players.map { it.copy(profileId = local.profileId) })
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
        val table = StandaloneTable(store, shortGameMs = 0)
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
        val table = StandaloneTable(store, shortGameMs = 0)
        assertTrue(table.createAccount("Alex"))
        assertTrue(table.createAccount("Alex"))
        val players = table.state.value.library.players
        assertNotEquals(players[0].profileId, players[1].profileId)
        assertTrue(players.all { LocalProfileIds.valid(it.profileId) })
        val prefs = LocalPersonalization("#123ABC", "cat", Base64.getEncoder().encodeToString(byteArrayOf(1, 2)), true)
        assertTrue(table.saveAccount(players[0].localId, "New name", prefs))
        val restored = StandaloneTable(store, shortGameMs = 0).state.value.library
        assertEquals("New name", restored.players[0].name)
        assertEquals(prefs, restored.players[0].personalization)
        assertEquals(players[0].profileId, restored.players[0].profileId)
        assertEquals("Alex", restored.players[1].name)
    }

    @Test fun `legacy UUID history gains stable compatible IDs once without losing results`() {
        val store = Store()
        store.library = """{"schema":1,"players":[{"localId":"old-uuid","name":"Alex"}],"history":[],"deliveries":[]}"""
        val first = StandaloneTable(store, shortGameMs = 0).state.value.library.players.single()
        val second = StandaloneTable(store, shortGameMs = 0).state.value.library.players.single()
        assertEquals("old-uuid", second.localId)
        assertEquals(first.profileId, second.profileId)
        assertEquals(2, JSONObject(store.library!!).getInt("schema"))
    }

    @Test fun `automatic imports require verified links scoped to the destination not public ID collisions`() = runTest {
        val store = Store()
        var now = 0L
        val table = StandaloneTable(store, wallClock = { now }, shortGameMs = 0)
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
        assertEquals(record, StandaloneTable(store, shortGameMs = 0).state.value.library.history.single())
        table.queueAutomaticImports("atlas-a", available)
        var requests = 0
        table.sendRecords("atlas-a") { requests++; null }
        assertEquals(0, requests)
    }

    @Test fun `guest delivery sends a match with one linked player and adds others once linked`() = runTest {
        val store = Store()
        var now = 0L
        val table = StandaloneTable(store, wallClock = { now }, shortGameMs = 0)
        table.addPlayer("Ana"); table.addPlayer("Ben")
        val players = table.state.value.library.players
        table.start(); now = 100_000
        table.control(com.turnhub.android.data.TabletSeat(9, 1), "win")
        val record = table.state.value.library.history.single()
        val available = players.map { it.profileId }.toSet()
        assertTrue(table.linkAccount(players[0].localId, "atlas-a", players[0].profileId))
        // Older Atlas firmware: every player must be linked.
        assertNull(table.state.value.library.automaticMapping(record, "atlas-a", available))
        table.queueAutomaticImports("atlas-a", available)
        assertEquals(DeliveryStatus.NEEDS_LINKING, table.state.value.library.delivery(record.recordId).status)

        table.queueAutomaticImports("atlas-a", available, guests = true)
        assertEquals(listOf(players[0].profileId, ""), table.state.value.library.delivery(record.recordId).profileIds)
        val sent = mutableListOf<List<Pair<String, String>>>()
        table.sendRecords("atlas-a") { sent += it; com.turnhub.android.data.RawResponse(200,
            """{"ok":true,"duplicate":false,"credited":1,"already":0,"guests":1,"unmatched":[]}""") }
        assertEquals("", sent.single().first { it.first == "profile1" }.second)
        assertEquals(DeliveryStatus.IMPORTED, table.state.value.library.delivery(record.recordId).status)
        // Survives a restart with its guest mapping.
        assertEquals(listOf(players[0].profileId, ""),
            StandaloneTable(store, shortGameMs = 0).state.value.library.delivery(record.recordId).profileIds)

        // Nothing new linked: not sent again.
        table.queueAutomaticImports("atlas-a", available, guests = true)
        assertEquals(DeliveryStatus.IMPORTED, table.state.value.library.delivery(record.recordId).status)
        // A remap of an already credited player is refused.
        assertFalse(table.queueImport(record.recordId, "atlas-a", listOf(players[1].profileId, players[0].profileId), available, guests = true))

        assertTrue(table.linkAccount(players[1].localId, "atlas-a", players[1].profileId))
        table.queueAutomaticImports("atlas-a", available, guests = true)
        val again = table.state.value.library.delivery(record.recordId)
        assertEquals(DeliveryStatus.PENDING, again.status)
        assertEquals(players.map { it.profileId }, again.profileIds)
        table.sendRecords("atlas-a") { sent += it; com.turnhub.android.data.RawResponse(200,
            """{"ok":true,"duplicate":false,"credited":1,"already":1,"guests":0,"unmatched":[]}""") }
        assertEquals(DeliveryStatus.IMPORTED, table.state.value.library.delivery(record.recordId).status)
        assertEquals(players[1].profileId, sent.last().first { it.first == "profile1" }.second)
        table.queueAutomaticImports("atlas-a", available, guests = true)
        assertEquals(DeliveryStatus.IMPORTED, table.state.value.library.delivery(record.recordId).status)
    }

    @Test fun `profile sync keeps keys and activity, and an Atlas copy never counts as a local change`() {
        val store = Store()
        var now = 5_000L
        val table = StandaloneTable(store, wallClock = { now }, shortGameMs = 0)
        assertTrue(table.createAccount("Ana"))
        val ana = table.state.value.library.players.single()
        assertEquals(0L, ana.lastActivityMs)
        // A tile background doesn't sync, so it isn't a synced change.
        now = 6_000
        assertTrue(table.saveAccount(ana.localId, "Ana", ana.personalization.copy(
            tileBackground = com.turnhub.android.domain.TileBackground(TileBackgroundStyle.GRADIENT))))
        assertEquals(0L, table.state.value.library.players.single().lastActivityMs)
        now = 7_000
        assertTrue(table.saveAccount(ana.localId, "Ana B", table.state.value.library.players.single().personalization))
        assertEquals(7_000L, table.state.value.library.players.single().lastActivityMs)

        val key = "0123456789ABCDEF0123456789ABCDEF"
        assertFalse(table.linkAccount(ana.localId, "THA-1", ana.profileId, "not-a-key"))
        assertTrue(table.linkAccount(ana.localId, "THA-1", ana.profileId, key))
        // Relinking the same profile without a new key keeps the old one.
        assertTrue(table.linkAccount(ana.localId, "THA-1", ana.profileId))
        assertEquals(key, table.state.value.library.players.single().links.single().linkKey)

        assertTrue(table.applySyncedProfile(ana.localId, "Ana from Atlas", "#112233", "crown", true, 9_000))
        val synced = table.state.value.library.players.single()
        assertEquals("Ana from Atlas", synced.name)
        assertEquals("#112233", synced.personalization.color)
        assertEquals("crown", synced.personalization.avatar)
        assertTrue(synced.personalization.reduceMotion)
        assertEquals(9_000L, synced.lastActivityMs)
        assertFalse(table.applySyncedProfile(ana.localId, "Bad", "red", null, false, 10_000))

        val restored = StandaloneTable(store, shortGameMs = 0).state.value.library.players.single()
        assertEquals(key, restored.links.single().linkKey)
        assertEquals(9_000L, restored.lastActivityMs)
        assertTrue(table.dropLinkKey(ana.localId, "THA-1"))
        assertNull(table.state.value.library.players.single().links.single().linkKey)
        assertEquals(ana.profileId, table.state.value.library.players.single().links.single().profileId)
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
        val table = StandaloneTable(store, shortGameMs = 0)
        table.createAccount("Ana")
        val before = table.state.value.library
        store.fail = true
        assertFalse(table.linkAccount(before.players[0].localId, "atlas", "AABBCCDD"))
        assertEquals(before, table.state.value.library)
        assertNotNull(table.state.value.storageProblem)
    }

    @Test fun `setting a PIN keeps the account's settings open until the app restarts`() {
        val store = Store()
        val table = StandaloneTable(store)
        // Players typed into the lobby get an account with no PIN; setting one used to lock it at once.
        table.addPlayer("Ana")
        val player = table.state.value.library.players.single()
        assertTrue(table.setAccountCredential(player.localId, LocalCredential.create("1234")))
        // The editor stays open, so the next change and a second PIN change both save.
        assertTrue(table.accountUnlocked(player.localId))
        assertTrue(table.saveAccount(player.localId, player.name, player.personalization.copy(avatar = "die")))
        assertTrue(table.setAccountCredential(player.localId, LocalCredential.create("5678")))
        assertFalse(StandaloneTable(store).accountUnlocked(player.localId))
    }

    @Test fun `a PIN can be removed and an account deleted, keeping its finished games`() = runTest {
        val store = Store()
        val table = StandaloneTable(store, shortGameMs = 0)
        table.addPlayer("Ana"); table.addPlayer("Bo")
        val ana = table.state.value.library.players.first { it.name == "Ana" }
        assertTrue(table.setAccountCredential(ana.localId, LocalCredential.create("1234")))
        assertTrue(table.clearAccountCredential(ana.localId))
        assertNull(table.state.value.library.players.first { it.localId == ana.localId }.credential)
        assertTrue(StandaloneTable(store).accountUnlocked(ana.localId))
        // Not while Ana is playing.
        table.start()
        assertFalse(table.deleteAccount(ana.localId))
        table.control(com.turnhub.android.data.TabletSeat(StandaloneGame.seatHandle(0).id, 1), "reset")
        assertTrue(table.deleteAccount(ana.localId))
        assertTrue(table.state.value.library.players.none { it.localId == ana.localId })
        assertTrue(table.state.value.game.players.none { it.localId == ana.localId })
        assertTrue(StandaloneTable(store).state.value.library.players.none { it.localId == ana.localId })
    }

    @Test fun `local account credentials preserve Atlas rules without storing plaintext and lock settings after restart`() {
        val store = Store()
        val table = StandaloneTable(store, shortGameMs = 0)
        assertTrue(table.createAccount("Ana", LocalCredential.create("1234")))
        val player = table.state.value.library.players.single()
        assertFalse(store.library!!.contains("\"1234\""))
        assertTrue(player.credential!!.verifies("1234"))
        assertFalse(player.credential.verifies("9999"))
        val restored = StandaloneTable(store, shortGameMs = 0)
        assertFalse(restored.accountUnlocked(player.localId))
        assertFalse(restored.saveAccount(player.localId, "Changed", LocalPersonalization()))
        assertFalse(restored.unlockAccount(player.localId, "9999"))
        assertTrue(restored.unlockAccount(player.localId, "1234"))
        assertTrue(restored.saveAccount(player.localId, "Changed", LocalPersonalization()))
        assertTrue(runCatching { LocalCredential.create("123") }.isFailure)
    }

    @Test fun `one device Admin account needs a PIN, keeps it, survives restart and is cleared when deleted`() {
        val store = Store()
        val table = StandaloneTable(store, shortGameMs = 0)
        assertTrue(table.createAccount("Guest"))
        val guest = table.state.value.library.players.single()
        // Admin needs a PIN or password.
        assertFalse(table.setDeviceAdmin(guest.localId))
        assertTrue(table.createAdminAccount("Ricky", LocalCredential.create("1234")))
        val ricky = table.state.value.library.players.last()
        assertTrue(table.state.value.library.isDeviceAdmin(ricky.localId))
        // Only one: setup can't make a second.
        assertFalse(table.createAdminAccount("Other", LocalCredential.create("5678")))
        assertFalse(table.clearAccountCredential(ricky.localId))

        val restored = StandaloneTable(store)
        assertEquals(ricky.localId, restored.state.value.library.deviceAdmin)
        assertFalse(restored.state.value.library.adminPromptDismissed)
        // Handing Admin on needs the current Admin unlocked.
        assertTrue(restored.createAccount("Ana", LocalCredential.create("2468")))
        val ana = restored.state.value.library.players.last()
        assertFalse(StandaloneTable(store).setDeviceAdmin(ana.localId))
        assertTrue(restored.unlockAccount(ricky.localId, "1234"))
        assertTrue(restored.setDeviceAdmin(ana.localId))
        assertTrue(restored.clearAccountCredential(ricky.localId))

        assertTrue(restored.dismissAdminPrompt())
        assertTrue(restored.deleteAccount(ana.localId))
        assertNull(restored.state.value.library.deviceAdmin)
        assertFalse(restored.state.value.library.adminPromptDismissed)
        assertNull(StandaloneTable(store).state.value.library.deviceAdmin)
    }
}
