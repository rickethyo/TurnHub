package com.turnhub.android.standalone

import com.turnhub.android.data.TabletSeat
import com.turnhub.android.data.RawResponse
import com.turnhub.android.protocol.GameProfile
import com.turnhub.android.protocol.TableState
import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertSame
import org.junit.Assert.assertTrue
import org.junit.Test
import kotlin.random.Random

class StandaloneGameTest {

    /** Always starts with player 1 (index 0). */
    private val firstSeat = object : Random() {
        override fun nextBits(bitCount: Int): Int = 0
    }

    private fun threePlayers(): StandaloneGame = StandaloneGame()
        .setFormat(GameProfile.MTG_COMMANDER)
        .addPlayer("Ana", "p-ana")
        .addPlayer("Ben")
        .addPlayer("Cy")
        .start(nowMs = 1_000, gameId = "g1", random = firstSeat)

    @Test
    fun `poison and commander damage rules are independent and live`() {
        var game = threePlayers().setAutoEliminateAtZeroLife(false, 1_000)
        val poisoned = game.changeCounter(0, "poison", 10, nowMs = 2_000)
        assertTrue(poisoned.players[0].eliminated)
        assertEquals(40, poisoned.players[0].life)
        game = game.setEliminationRule("autoEliminateAtPoisonLimit", 0, 1_000).changeCounter(0, "poison", 12)
        assertTrue(!game.players[0].eliminated)
        assertTrue(game.togglePause(2_000).setEliminationRule("autoEliminateAtPoisonLimit", 1, 3_000).players[0].eliminated)
        val custom = threePlayers().setEliminationRule("poisonLimit", 20, 1_000).changeCounter(0, "poison", 19)
        assertTrue(!custom.players[0].eliminated)
        assertTrue(custom.setEliminationRule("poisonLimit", 19, 2_000).players[0].eliminated)
        val damage = threePlayers().setAutoEliminateAtZeroLife(false, 1_000)
            .commanderDamage(0, 1, 1, 20).commanderDamage(0, 1, 2, 20).commanderDamage(0, 2, 1, 20)
        assertTrue(!damage.players[0].eliminated) // Never pool sources or partners.
        assertTrue(damage.commanderDamage(0, 1, 2, 1).players[0].eliminated)
        val protected = threePlayers().setEliminationRule("autoEliminateAtCommanderDamage", 0, 1_000).commanderDamage(0, 1, 1, 21)
        assertTrue(!protected.players[0].eliminated)
        assertTrue(protected.setEliminationRule("autoEliminateAtCommanderDamage", 1, 2_000).players[0].eliminated)
    }

    @Test
    fun `statuses transfer independently and counters tax and rules survive recovery`() {
        val game = threePlayers().claimMonarch(0).changeCounter(1, "initiative", 1).changeCounter(2, "initiative", 1)
            .changeCounter(0, "city-blessing", 1).changeCounter(1, "city-blessing", 1)
            .changeCounter(0, "energy", 4).changeCounter(0, "energy", -1).changeCounter(0, "experience", 2)
            .changeCounter(0, "commander-casts", 2, 1).changeCounter(0, "commander-casts", 1, 2)
            .setEliminationRule("poisonLimit", 17, 1_000).setEliminationRule("autoEliminateAtCommanderDamage", 0, 1_000)
        assertEquals(0, game.monarch)
        assertEquals(2, game.initiative)
        assertTrue(game.players[0].counters.cityBlessing && game.players[1].counters.cityBlessing)
        assertEquals(4, game.players[0].counters.commanderTax(1))
        assertEquals(2, game.players[0].counters.commanderTax(2))
        val restored = StandaloneTable.gameFromJson(StandaloneTable.gameToJson(game))
        assertEquals(game, restored)
        assertEquals(game.players[0].counters, restored.toSummary(2_000, 2_000).players[0].counters)
        assertEquals(0, restored.concede(2, 2_000).initiative) // CR 725.4: to the active player.
        val reset = game.reset()
        assertNull(reset.initiative)
        assertEquals(com.turnhub.android.protocol.PlayerCounters(), reset.players[0].counters)
        assertEquals(17, reset.poisonLimit)
    }

    @Test
    fun `counter corrections reject negative overflow format and inactive seats`() {
        val game = threePlayers()
        assertSame(game, game.changeCounter(0, "energy", -1))
        assertSame(game, game.changeCounter(0, "poison", Int.MAX_VALUE))
        assertSame(game, game.changeCounter(0, "commander-casts", 1, 3))
        assertSame(game, game.changeCounter(0, "city-blessing", 2))
        assertSame(game, game.setEliminationRule("poisonLimit", 0, 2_000))
        val out = game.eliminate(0, 2_000)
        assertSame(out, out.changeCounter(0, "experience", 1))
        val generic = game.reset().setFormat(GameProfile.GENERIC).start(3_000, "g2", firstSeat)
        assertSame(generic, generic.changeCounter(0, "initiative", 1))
    }

    @Test
    fun `monarch transfers to one living player and survives pause and storage`() {
        val game = threePlayers().claimMonarch(0).claimMonarch(1).togglePause(2_000)
        assertEquals(1, game.monarch)
        assertEquals(2, game.toSummary(2_000, 2_000).monarchPlayerNumber)
        val restored = StandaloneTable.gameFromJson(StandaloneTable.gameToJson(game))
        assertEquals(1, restored.monarch)
        assertEquals(2, restored.claimMonarch(2).monarch)
        assertSame(restored, restored.claimMonarch(99))
    }

    @Test
    fun `crown passes on by CR 724_4 and clears for a new match`() {
        val game = threePlayers().claimMonarch(0)
        // The active monarch leaves: the next player in turn order takes it.
        assertEquals(1, game.concede(0, 2_000).monarch)
        assertEquals(1, game.eliminate(0, 2_000).monarch)
        assertEquals(1, game.changeLife(0, -40, 2_000).monarch)
        // Another player leaves: the active player takes it.
        assertEquals(0, threePlayers().claimMonarch(2).concede(2, 2_000).monarch)
        assertEquals(0, threePlayers().claimMonarch(1).changeLife(1, -40, 2_000).monarch)
        assertNull(game.reset().monarch)
        assertNull(game.claimWin(1, 2_000).rematch(3_000, "g2", firstSeat).monarch)
        val out = game.eliminate(0, 2_000)
        assertSame(out, out.claimMonarch(0))
    }

    @Test
    fun `monarch is only available in active Magic games`() {
        val lobby = StandaloneGame().addPlayer("Ana").addPlayer("Ben")
        assertSame(lobby, lobby.claimMonarch(0))
        for (profile in listOf(GameProfile.GENERIC, GameProfile.YUGIOH)) {
            val game = lobby.setFormat(profile).start(1_000, "g", firstSeat)
            assertSame(game, game.claimMonarch(0))
        }
        val magic = lobby.setFormat(GameProfile.MTG).start(1_000, "g", firstSeat)
        assertEquals(0, magic.claimMonarch(0).monarch)
        val over = magic.claimWin(0, 2_000)
        assertSame(over, over.claimMonarch(1))
    }

    @Test
    fun `zero life eliminates by default and advances the turn`() {
        val game = threePlayers().changeLife(0, -40, 2_000)
        assertTrue(game.players[0].eliminated)
        assertTrue(!game.players[0].conceded)
        assertEquals(1, game.active)
        val won = game.commanderDamage(1, 2, 1, 40, 3_000)
        assertEquals(TableState.GAME_OVER, won.state)
        assertEquals(2, won.winner)
    }

    @Test
    fun `zero life policy can change while paused and checks everyone together`() {
        var game = threePlayers().setAutoEliminateAtZeroLife(false, 1_000)
        for (i in game.players.indices) game = game.changeLife(i, -41, 2_000)
        assertTrue(game.players.none { it.eliminated })
        game = game.togglePause(3_000).setAutoEliminateAtZeroLife(true, 4_000)
        assertEquals(TableState.GAME_OVER, game.state)
        assertNull(game.winner)
        assertTrue(game.players.all { it.eliminated && !it.conceded })
    }

    @Test
    fun `concession and elimination preserve distinct reasons in saved games and records`() {
        val game = threePlayers().concede(0, 2_000).eliminate(1, 3_000)
        val restored = StandaloneTable.gameFromJson(StandaloneTable.gameToJson(game))
        assertTrue(restored.players[0].conceded)
        assertTrue(!restored.players[1].conceded)
        val record = GameRecord.fromJson(game.record()!!.toJson())!!
        assertTrue(record.players[0].conceded)
        assertTrue(!record.players[1].conceded)
        assertTrue(record.importFields("THA-1", listOf("1", "2", "3")).contains("conceded0" to "1"))
        val disabled = threePlayers().setAutoEliminateAtZeroLife(false, 1_000)
        assertTrue(!StandaloneTable.gameFromJson(StandaloneTable.gameToJson(disabled)).autoEliminateAtZeroLife)
    }

    @Test
    fun `a game starts everyone on the format's life with a first player`() {
        val game = threePlayers()
        assertEquals(TableState.RUNNING, game.state)
        assertEquals(listOf(40, 40, 40), game.players.map { it.life })
        assertEquals(0, game.active)
        assertEquals(0, game.starter)
    }

    @Test
    fun `the lobby needs two players and refuses duplicates of one profile`() {
        val one = StandaloneGame().addPlayer("Ana", "p-ana")
        assertSame(one, one.start(0, "g", firstSeat))
        assertSame(one, one.addPlayer("Ana again", "p-ana"))
        assertSame(one, one.addPlayer("   "))
    }

    @Test
    fun `turn order can be changed in the lobby`() {
        val game = StandaloneGame().addPlayer("Ana").addPlayer("Ben").movePlayer(1, -1)
        assertEquals(listOf("Ben", "Ana"), game.players.map { it.name })
    }

    @Test
    fun `passing moves the turn clockwise and counts the finished turn`() {
        val game = threePlayers().pass(0, 61_000)
        assertEquals(1, game.active)
        assertEquals(1, game.players[0].turnsCompleted)
        assertEquals(60_000, game.turnStartedAtElapsedMs)
        assertEquals(60_000, game.players[0].turnMs)
        assertEquals(60_000, game.players[0].fastestTurnMs)
        // Only the active player passes.
        assertSame(game, game.pass(0, 62_000))
    }

    @Test
    fun `commander damage costs life and never goes below zero`() {
        val game = threePlayers().commanderDamage(index = 0, source = 1, commander = 1, delta = 7)
        assertEquals(33, game.players[0].life)
        assertEquals(listOf(7, 0), game.players[0].commanderDamage[1])
        val healed = game.commanderDamage(0, 1, 1, -10)
        assertEquals(listOf(0, 0), healed.players[0].commanderDamage[1])
        assertEquals(40, healed.players[0].life)
        assertSame(game, game.commanderDamage(0, 0, 1, 1))
    }

    @Test
    fun `conceding skips the player and the last one standing wins`() {
        val game = threePlayers().concede(0, 2_000)
        assertEquals(1, game.active)
        assertTrue(game.players[0].eliminated)
        assertEquals(1, game.players[0].outOrder)
        val over = game.concede(2, 3_000)
        assertEquals(TableState.GAME_OVER, over.state)
        assertEquals(1, over.winner)
        assertEquals(2_000, over.elapsedMs)
    }

    @Test
    fun `pausing stops the game clock`() {
        val paused = threePlayers().togglePause(11_000)
        assertEquals(10_000, paused.gameElapsedMs(50_000))
        val resumed = paused.togglePause(50_000)
        assertEquals(15_000, resumed.gameElapsedMs(55_000))
    }

    @Test
    fun `a finished game becomes a record for Atlas`() {
        val record = threePlayers().changeLife(1, -5).pass(0, 5_000).claimWin(1, 9_000).record()!!
        assertEquals("g1", record.recordId)
        assertEquals(1, record.winner)
        assertEquals(8_000, record.durationMs)
        assertEquals("p-ana", record.players[0].profileId)
        assertNull(record.players[1].profileId)
        assertEquals(35, record.players[1].finalLife)
        assertEquals(record, GameRecord.fromJson(record.toJson()))
    }

    @Test
    fun `the table keeps the game and each finished game once`() = runTest {
        val store = MemoryStore()
        var clock = 1_000L
        var ids = 0
        val table = StandaloneTable(store, { clock }, { "g${++ids}" })
        table.addPlayer("Ana")
        table.addPlayer("Ben")
        table.start()
        val first = table.state.value.game.active!!
        clock = 4_000
        table.life(TabletSeat(StandaloneGame.seatHandle(first).id, 1), -3)
        table.control(TabletSeat(StandaloneGame.seatHandle(first).id, 1), "win")
        assertEquals(1, table.state.value.library.history.size)
        table.control(TabletSeat(StandaloneGame.seatHandle(first).id, 1), "win")
        assertEquals(1, table.state.value.library.history.size)

        // A new table on the same store picks up where this one stopped.
        val again = StandaloneTable(store, { clock }, { "g${++ids}" })
        assertEquals(table.state.value.game, again.state.value.game)
        assertEquals(table.state.value.library, again.state.value.library)

        again.control(TabletSeat(StandaloneGame.seatHandle(0).id, 1), "rematch")
        assertEquals(TableState.RUNNING, again.state.value.game.state)
        again.control(TabletSeat(StandaloneGame.seatHandle(0).id, 1), "draw")
        assertEquals(2, again.state.value.library.history.size)
        assertNull(again.state.value.library.history[1].winner)
        val id = again.state.value.library.history.first().recordId
        assertTrue(again.queueImport(id, "THA-A", listOf("p-a", "p-b"), setOf("p-a", "p-b")))
        again.sendRecords("THA-A") { RawResponse(200, "{\"ok\":true,\"duplicate\":false,\"credited\":2,\"unmatched\":[]}") }
        assertEquals(2, again.state.value.library.history.size)
        assertEquals(DeliveryStatus.IMPORTED, again.state.value.library.delivery(id).status)
        assertEquals(again.state.value.library, StandaloneTable(store).state.value.library)
    }

    @Test
    fun `same names are separate identities and saved picks reuse identity across matches`() = runTest {
        val store = MemoryStore()
        val table = StandaloneTable(store)
        table.addPlayer("Ana")
        table.addPlayer("Ana")
        val ids = table.state.value.game.players.map { it.localId!! }
        assertEquals(2, ids.distinct().size)
        table.selectPlayer(ids.first()) // Cannot seat the same local player twice.
        assertEquals(2, table.state.value.game.players.size)
        table.start()
        val seat = TabletSeat(StandaloneGame.seatHandle(0).id, 1)
        table.control(seat, "win")
        table.control(seat, "rematch")
        assertEquals(ids, table.state.value.game.players.map { it.localId })
        table.control(seat, "draw")
        table.control(seat, "reset")
        table.removePlayer(0)
        table.selectPlayer(ids.first())
        assertEquals(ids.reversed(), table.state.value.game.players.map { it.localId })
        val library = StandaloneTable(store).state.value.library
        assertEquals(LocalTotals(2, 1, 1), library.totals(ids.first()))
        assertEquals(LocalTotals(2, 0, 1), library.totals(ids.last()))
    }

    @Test
    fun `history is not pruned at the former queue limit`() = runTest {
        val table = StandaloneTable(MemoryStore())
        table.addPlayer("Ana")
        table.addPlayer("Ben")
        val seat = TabletSeat(StandaloneGame.seatHandle(0).id, 1)
        repeat(205) {
            table.start()
            table.control(seat, "draw")
            table.control(seat, "reset")
        }
        assertEquals(205, table.state.value.library.history.size)
        assertEquals(205, table.state.value.library.history.map { it.recordId }.distinct().size)
    }

    @Test
    fun `legacy queue is retained without inventing player or Atlas links`() {
        val store = MemoryStore()
        store.records = org.json.JSONArray().put(threePlayers().claimWin(0, 2_000).record()!!.toJson()).toString()
        val table = StandaloneTable(store)
        assertEquals(1, table.state.value.library.history.size)
        assertTrue(table.state.value.library.players.isEmpty())
        assertEquals(DeliveryStatus.NEEDS_LINKING, table.state.value.library.delivery("g1").status)
        assertNull(table.state.value.library.history.first().players.first().localId)
        table.addPlayer("Ana")
        assertEquals(0, table.state.value.library.totals(table.state.value.library.players.first().localId).played)
    }

    @Test
    fun `corrupt or future library is kept untouched and local edits fail closed`() {
        for (json in listOf("not json", "{\"schema\":99}")) {
            val store = MemoryStore().apply { library = json }
            val table = StandaloneTable(store)
            assertTrue(table.state.value.storageProblem != null)
            table.addPlayer("Ana")
            assertEquals(json, store.library)
            assertTrue(table.state.value.library.players.isEmpty())
        }
    }

    @Test
    fun `game reconstruction preserves running paused clocks and local identities`() {
        val id = "local-ana"
        val running = StandaloneGame().addPlayer("Ana", localId = id).addPlayer("Ben", localId = "local-ben")
            .start(1_000, "match-123", firstSeat).changeLife(0, -2).pass(0, 5_000)
        val restored = StandaloneTable.gameFromJson(StandaloneTable.gameToJson(running))
        assertEquals(running, restored)
        assertEquals(9_000, restored.gameElapsedMs(10_000))
        val paused = restored.togglePause(10_000)
        val pausedAgain = StandaloneTable.gameFromJson(StandaloneTable.gameToJson(paused))
        assertEquals(9_000, pausedAgain.gameElapsedMs(20_000))
        assertEquals(id, pausedAgain.players.first().localId)
    }

    @Test
    fun `delivery success rejection and transient failure preserve history and local totals`() = runTest {
        val store = MemoryStore()
        val table = StandaloneTable(store)
        table.addPlayer("Ana"); table.addPlayer("Ben")
        val seat = TabletSeat(StandaloneGame.seatHandle(0).id, 1)
        repeat(3) {
            table.start(); table.control(seat, "draw"); table.control(seat, "reset")
        }
        val history = table.state.value.library.history
        history.forEach { assertTrue(table.queueImport(it.recordId, "THA-A", listOf("p-a", "p-b"), setOf("p-a", "p-b"))) }
        val answers = ArrayDeque(listOf(
            RawResponse(200, "{\"ok\":true,\"duplicate\":true,\"credited\":0,\"unmatched\":[]}"),
            RawResponse(400, "{\"error\":\"Profile was archived\"}"), RawResponse(503, "{}")))
        assertEquals(1, table.sendRecords("THA-A") { fields ->
            assertEquals("THA-A", fields.toMap()["atlasId"])
            assertEquals("p-a", fields.toMap()["profile0"])
            answers.removeFirst()
        })
        val library = StandaloneTable(store).state.value.library
        assertEquals(history, library.history)
        assertEquals(listOf(DeliveryStatus.IMPORTED, DeliveryStatus.REJECTED, DeliveryStatus.PENDING),
            history.map { library.delivery(it.recordId).status })
        assertEquals("Profile was archived", library.delivery(history[1].recordId).reason)
        assertEquals(LocalTotals(3, 0, 3), library.totals(history.first().players.first().localId!!))
        var calls = 0
        table.sendRecords("THA-A") { calls++; null }
        assertEquals(1, calls) // Rejection is not automatically retried.
        assertEquals(history, table.state.value.library.history)
    }

    @Test
    fun `failed local writes report a problem and do not publish unsaved completion`() = runTest {
        val store = MemoryStore()
        val table = StandaloneTable(store)
        table.addPlayer("Ana"); table.addPlayer("Ben"); table.start()
        val before = table.state.value.game
        val saved = store.library
        store.failWrites = true
        table.control(TabletSeat(StandaloneGame.seatHandle(0).id, 1), "draw")
        assertEquals(before, table.state.value.game)
        assertEquals(saved, store.library)
        assertTrue(table.state.value.storageProblem != null)
        assertTrue(table.state.value.library.history.isEmpty())
    }

    @Test
    fun `scope mapping and uncertain acknowledgements fail closed without matching names`() = runTest {
        val table = StandaloneTable(MemoryStore())
        table.addPlayer("Ana"); table.addPlayer("Ana")
        val seat = TabletSeat(StandaloneGame.seatHandle(0).id, 1)
        table.start(); table.control(seat, "draw")
        val id = table.state.value.library.history.single().recordId
        assertTrue(!table.queueImport(id, "THA-A", listOf("p-a", "p-a"), setOf("p-a")))
        assertTrue(!table.queueImport(id, "THA-A", listOf("p-a", "foreign"), setOf("p-a", "p-b")))
        assertTrue(table.queueImport(id, "THA-A", listOf("p-a", "p-b"), setOf("p-a", "p-b")))
        assertTrue(!table.queueImport(id, "THA-B", listOf("p-a", "p-b"), setOf("p-a", "p-b")))
        var calls = 0
        assertEquals(0, table.sendRecords("THA-B") { calls++; RawResponse(200, "{}") })
        assertEquals(0, calls)
        assertEquals(0, table.sendRecords("THA-A") { RawResponse(200, "{}") })
        assertEquals(DeliveryStatus.PENDING, table.state.value.library.delivery(id).status)
        table.sendRecords("THA-A") { RawResponse(200, "{\"ok\":true,\"duplicate\":false,\"credited\":2,\"unmatched\":[]}") }
        assertEquals(DeliveryStatus.IMPORTED, table.state.value.library.delivery(id).status)
        assertTrue(!table.queueImport(id, "THA-A", listOf("p-a", "p-b"), setOf("p-a", "p-b")))
        assertEquals(0, table.sendRecords("THA-A") { calls++; null })
        assertEquals(0, calls)
    }

    private class MemoryStore : StandaloneStore {
        var game: String? = null
        var records: String? = null
        var library: String? = null
        var failWrites = false
        override fun loadLibrary() = library
        override fun saveSnapshot(game: String, library: String): Boolean {
            if (failWrites) return false
            this.game = game
            this.library = library
            return true
        }
        override fun loadGame() = game
        override fun loadRecords() = records
    }
}
