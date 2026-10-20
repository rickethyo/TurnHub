package com.turnhub.android.standalone

import com.turnhub.android.data.TabletSeat
import com.turnhub.android.data.RawResponse
import com.turnhub.android.domain.DayNight
import com.turnhub.android.domain.StarCommander
import com.turnhub.android.protocol.GameProfile
import com.turnhub.android.protocol.TableState
import com.turnhub.android.protocol.TurnTimer
import com.turnhub.android.protocol.TurnTimerPhase
import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
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

    /** [threePlayers] with house rules, so the official Commander limits can change. */
    private fun houseThree(): StandaloneGame = StandaloneGame()
        .setFormat(GameProfile.MTG_COMMANDER)
        .setHouseRules(true)
        .addPlayer("Ana", "p-ana")
        .addPlayer("Ben")
        .addPlayer("Cy")
        .start(nowMs = 1_000, gameId = "g1", random = firstSeat)

    private fun teams(): StandaloneGame = StandaloneGame()
        .setFormat(GameProfile.MTG_COMMANDER)
        .addPlayer("Ana").addPlayer("Ben").addPlayer("Cy").addPlayer("Dee")
        .setTwoHeadedGiant(true)

    @Test
    fun `clear players is lobby only and preserves game settings`() {
        val lobby = teams().setTurnTimer(60_000).setStartingLife(75)
        val cleared = lobby.clearPlayers()
        assertEquals(lobby.copy(players = emptyList(), revision = lobby.revision + 1), cleared)
        assertTrue(!cleared.canStart)
        assertSame(cleared, cleared.clearPlayers())
        val running = lobby.start(1_000, "g", firstSeat)
        assertSame(running, running.clearPlayers())
        val paused = running.togglePause(2_000)
        assertSame(paused, paused.clearPlayers())
        val finished = running.claimWin(0, 3_000)
        assertSame(finished, finished.clearPlayers())
    }

    @Test
    fun `clear players persists once and retains accounts and history for reselection`() = runTest {
        val store = MemoryStore()
        val table = StandaloneTable(store, shortGameMs = 0)
        table.addPlayer("Ana")
        table.addPlayer("Ben")
        table.start()
        table.control(TabletSeat(StandaloneGame.seatHandle(0).id, 1), "win")
        table.control(TabletSeat(StandaloneGame.seatHandle(0).id, 1), "reset")
        val library = table.state.value.library
        assertEquals(1, library.history.size)
        library.players.forEach { table.selectPlayer(it.localId) }
        table.setTurnTimer(60_000)
        val game = table.state.value.game
        val writes = store.writes
        table.clearPlayers()
        assertEquals(writes + 1, store.writes)
        assertEquals(game.clearPlayers(), table.state.value.game)
        assertEquals(library, table.state.value.library)
        val restored = StandaloneTable(store, shortGameMs = 0)
        assertEquals(table.state.value, restored.state.value)
        restored.selectPlayer(library.players.first().localId)
        assertEquals(library.players.first().localId, restored.state.value.game.players.single().localId)
        assertEquals(library, restored.state.value.library)
    }

    @Test
    fun `two-headed giant sets team life and poison and needs an even table of four`() {
        val lobby = teams()
        assertEquals(60, lobby.startingLife)
        assertEquals(15, lobby.poisonLimit)
        assertEquals(30, lobby.setFormat(GameProfile.MTG).startingLife)
        assertTrue(!lobby.setFormat(GameProfile.GENERIC).twoHeadedGiant)
        val three = lobby.removePlayer(3)
        assertSame(three, three.start(0, "g", firstSeat))
        val off = lobby.setTwoHeadedGiant(false)
        assertEquals(40, off.startingLife)
        assertEquals(10, off.poisonLimit)
    }

    @Test
    fun `two-headed giant teams share life, poison and the turn, and leave together`() {
        var game = teams().start(1_000, "g", firstSeat)
        // Either teammate passes the team's turn; it goes to the next team, credited to both.
        game = game.pass(1, 31_000)
        assertEquals(2, game.active)
        assertEquals(listOf(1, 1, 0, 0), game.players.map { it.turnsCompleted })
        assertSame(game, game.pass(0, 32_000))
        // Life and poison are the team's.
        game = game.changeLife(2, -5).changeCounter(3, "poison", 2)
        assertEquals(listOf(60, 60, 55, 55), game.players.map { it.life })
        assertEquals(listOf(0, 0, 2, 2), game.players.map { it.counters.poison })
        // Commander damage counts per player but costs the team's life.
        game = game.commanderDamage(0, 2, 1, 3)
        assertEquals(listOf(57, 57, 55, 55), game.players.map { it.life })
        assertEquals(listOf(3, 0), game.players[0].commanderDamage[2])
        // One teammate conceding takes the team out; the last team wins, both teammates.
        game = game.concede(3, 40_000)
        assertEquals(TableState.GAME_OVER, game.state)
        val record = game.record()!!
        assertTrue(record.won(0) && record.won(1) && !record.won(2) && !record.won(3))
        assertEquals(1, game.toSummary(40_000, 0).players[1].team)
    }

    @Test
    fun `turn timer counts down, warns, expires without passing and survives recovery`() {
        val lobby = StandaloneGame().addPlayer("Ana").addPlayer("Ben")
        assertSame(lobby, lobby.setTurnTimer(10_000)) // Below Atlas's 15 s minimum.
        assertSame(lobby, lobby.setTurnTimer(60_500)) // Whole seconds only.
        val game = lobby.setTurnTimer(60_000).start(nowMs = 1_000, gameId = "g", random = firstSeat)
        assertSame(game, game.setTurnTimer(0)) // Fixed once the game starts.
        assertEquals(TurnTimer(TurnTimerPhase.NORMAL, 60_000), game.turnTimer(1_000))
        assertEquals(TurnTimer(TurnTimerPhase.WARNING, 10_000), game.turnTimer(51_000))
        assertEquals(TurnTimer(TurnTimerPhase.EXPIRED, 0), game.turnTimer(90_000))
        assertEquals(0, game.active) // Expiry never passes the turn.
        val paused = game.togglePause(31_000)
        assertEquals(30_000L, paused.turnTimer(500_000).remainingMs) // Pause freezes it.
        val next = game.pass(0, 70_000)
        assertEquals(TurnTimer(TurnTimerPhase.NORMAL, 60_000), next.turnTimer(70_000)) // A new turn restarts it.
        val summary = next.toSummary(80_000, 0)
        assertEquals(60_000L, summary.settings.turnTimerMs)
        assertEquals(50_000L, summary.turnTimer.remainingMs)
        assertEquals(60_000L, StandaloneTable.gameFromJson(StandaloneTable.gameToJson(next)).turnTimerMs)
        assertEquals(60_000L, next.reset().turnTimerMs)
        val untimed = lobby.start(nowMs = 0, gameId = "u", random = firstSeat)
        assertEquals(TurnTimer(TurnTimerPhase.NORMAL, null), untimed.turnTimer(299_999))
        assertEquals(TurnTimer(TurnTimerPhase.LONG_TURN, null), untimed.turnTimer(300_000))
    }

    @Test
    fun `local stats summarize turns, starts and exits`() {
        val game = StandaloneGame().addPlayer("Ana", localId = "a").addPlayer("Ben", localId = "b").addPlayer("Cy", localId = "c")
            .start(nowMs = 0, gameId = "g", random = firstSeat)
            .pass(0, 10_000).pass(1, 40_000).pass(2, 45_000).pass(0, 75_000)
            .concede(2, 80_000).claimWin(0, 100_000)
        val library = LocalLibrary().withRecord(game.record()!!)
        val ana = library.stats("a")
        assertEquals(LocalTotals(1, 1, 0), ana.totals)
        assertEquals(1, ana.started)
        assertEquals(2, ana.turnsCompleted)
        assertEquals(20_000L, ana.averageTurnMs)
        assertEquals(10_000L, ana.fastestTurnMs)
        assertEquals(30_000L, ana.longestTurnMs)
        assertEquals(100_000L, ana.averageGameMs)
        assertEquals(1, library.stats("c").conceded)
        assertEquals(0, library.stats("c").eliminated)
        assertEquals(0, library.stats("nobody").totals.played)
    }

    @Test
    fun `poison and commander damage rules are independent and live`() {
        var game = threePlayers().setAutoEliminateAtZeroLife(false, 1_000)
        val poisoned = game.changeCounter(0, "poison", 10, nowMs = 2_000)
        assertTrue(poisoned.players[0].eliminated)
        assertEquals(40, poisoned.players[0].life)
        game = game.setEliminationRule("autoEliminateAtPoisonLimit", 0, 1_000).changeCounter(0, "poison", 12)
        assertTrue(!game.players[0].eliminated)
        assertTrue(game.togglePause(2_000).setEliminationRule("autoEliminateAtPoisonLimit", 1, 3_000).players[0].eliminated)
        val locked = threePlayers()
        assertEquals(locked, locked.setEliminationRule("poisonLimit", 20, 1_000))
        val custom = houseThree().setEliminationRule("poisonLimit", 20, 1_000).changeCounter(0, "poison", 19)
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
        val game = houseThree().claimMonarch(0).changeCounter(1, "initiative", 1).changeCounter(2, "initiative", 1)
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
    fun `treasure rad and day or night are kept and survive recovery`() {
        val game = threePlayers().changeCounter(0, "treasure", 3).changeCounter(0, "treasure", -1)
            .changeCounter(1, "rad", 4).setDayNight(DayNight.DAY).setDayNight(DayNight.NIGHT)
        assertEquals(2, game.players[0].counters.treasure)
        assertEquals(4, game.players[1].counters.rad)
        assertSame(game, game.changeCounter(0, "rad", -1))
        assertEquals(DayNight.NIGHT, game.toSummary(2_000, 2_000).dayNight)
        val restored = StandaloneTable.gameFromJson(StandaloneTable.gameToJson(game))
        assertEquals(game, restored)
        // A new game starts with neither; a game without Magic rules never reports it.
        assertEquals(DayNight.NEITHER, game.claimWin(0, 3_000).rematch(4_000, "g2", firstSeat).dayNight)
        val generic = game.reset().setFormat(GameProfile.GENERIC).start(3_000, "g3", firstSeat)
        assertSame(generic, generic.setDayNight(DayNight.DAY))
        assertNull(generic.toSummary(3_000, 3_000).dayNight)
    }

    @Test
    fun `rad counters trigger at the start of their player's turn until resolved`() {
        val before = threePlayers().changeCounter(1, "rad", 4)
        assertEquals(0, before.active)
        val due = before.pass(0, 2_000)
        assertEquals(listOf(1), due.radDue)
        assertEquals(listOf(2), due.toSummary(2_000, 2_000).radDuePlayerNumbers)
        assertEquals(due, StandaloneTable.gameFromJson(StandaloneTable.gameToJson(due)))
        // No more nonlands than cards milled, and only the player whose rad is due.
        assertSame(due, due.changeCounter(1, StandaloneGame.RAD_KIND, 5))
        assertSame(due, due.changeCounter(0, StandaloneGame.RAD_KIND, 0))
        val resolved = due.changeCounter(1, StandaloneGame.RAD_KIND, 3, nowMs = 2_000)
        assertEquals(due.players[1].life - 3, resolved.players[1].life)
        assertEquals(1, resolved.players[1].counters.rad)
        assertTrue(resolved.radDue.isEmpty())
        // All lands just clears the reminder; the next turn without rad has none.
        assertEquals(4, due.changeCounter(1, StandaloneGame.RAD_KIND, 0).players[1].counters.rad)
        assertTrue(resolved.pass(1, 3_000).radDue.isEmpty())
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
    fun `seats can be swapped in the lobby only`() {
        val lobby = StandaloneGame().addPlayer("Ana").addPlayer("Ben").addPlayer("Cy")
        assertEquals(listOf("Cy", "Ben", "Ana"), lobby.swapPlayers(0, 2).players.map { it.name })
        assertEquals(lobby, lobby.swapPlayers(1, 1))
        assertEquals(lobby, lobby.swapPlayers(0, 3))
        val started = lobby.start(0, "g")
        assertEquals(started, started.swapPlayers(0, 1))
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
        val table = StandaloneTable(store, { clock }, { "g${++ids}" }, shortGameMs = 0)
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
        val again = StandaloneTable(store, { clock }, { "g${++ids}" }, shortGameMs = 0)
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
        assertEquals(again.state.value.library, StandaloneTable(store, shortGameMs = 0).state.value.library)
    }

    @Test
    fun `same names are separate identities and saved picks reuse identity across matches`() = runTest {
        val store = MemoryStore()
        val table = StandaloneTable(store, shortGameMs = 0)
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
        val library = StandaloneTable(store, shortGameMs = 0).state.value.library
        assertEquals(LocalTotals(2, 1, 1), library.totals(ids.first()))
        assertEquals(LocalTotals(2, 0, 1), library.totals(ids.last()))
    }

    @Test
    fun `games under a minute wait to be saved and a start can be undone for three minutes`() = runTest {
        var clock = 0L
        var ids = 0
        val store = MemoryStore()
        val table = StandaloneTable(store, { clock }, { "id${ids++}" })
        table.addPlayer("Ana"); table.addPlayer("Ben"); table.chooseStarter(1)
        val lobby = table.state.value.game
        val seat = TabletSeat(StandaloneGame.seatHandle(0).id, 1)
        table.start()
        clock = 59_000
        table.control(seat, "draw")
        assertTrue(table.state.value.library.history.isEmpty())
        assertNotNull(table.unsavedShortGame())
        assertTrue(StandaloneTable(store, { clock }).state.value.library.history.isEmpty())
        assertTrue(table.saveShortGame())
        assertEquals(1, table.state.value.library.history.size)
        assertNull(table.unsavedShortGame())
        assertTrue(!table.saveShortGame())

        table.control(seat, "rematch")
        clock += StandaloneGame.UNDO_START_MS + 1
        table.control(seat, "back-to-setup")
        assertEquals(TableState.RUNNING, table.state.value.game.state)
        table.control(seat, "pause")
        table.control(seat, "draw")
        assertEquals(2, table.state.value.library.history.size) // Over a minute: counted.
        table.control(seat, "rematch")
        clock += 30_000
        table.control(seat, "back-to-setup")
        val back = table.state.value.game
        assertEquals(TableState.LOBBY, back.state)
        assertEquals(lobby.players, back.players)
        assertEquals(2, table.state.value.library.history.size)
    }

    @Test
    fun `history is not pruned at the former queue limit`() = runTest {
        val table = StandaloneTable(MemoryStore(), shortGameMs = 0)
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
        val table = StandaloneTable(store, shortGameMs = 0)
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
            val table = StandaloneTable(store, shortGameMs = 0)
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
        val table = StandaloneTable(store, shortGameMs = 0)
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
        val library = StandaloneTable(store, shortGameMs = 0).state.value.library
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
        val table = StandaloneTable(store, shortGameMs = 0)
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
        val table = StandaloneTable(MemoryStore(), shortGameMs = 0)
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

    // --- Star Commander ---------------------------------------------------------------

    private fun starLobby(): StandaloneGame = StandaloneGame()
        .setFormat(GameProfile.MTG_STAR_COMMANDER)
        .addPlayer("Ana", localId = "a").addPlayer("Ben", localId = "b").addPlayer("Cy", localId = "c")
        .addPlayer("Dee", localId = "d").addPlayer("Eve", localId = "e")

    private fun star(): StandaloneGame = starLobby().start(1_000, "s1", firstSeat)

    /** Takes [seats] out together in one rule-change batch (zero life, then the rule back on). */
    private fun StandaloneGame.knockOut(vararg seats: Int): StandaloneGame {
        var game = setAutoEliminateAtZeroLife(false, 2_000)
        seats.forEach { game = game.changeLife(it, -game.players[it].life) }
        return game.setAutoEliminateAtZeroLife(true, 3_000)
    }

    @Test
    fun `star commander needs exactly five players, starts at 40 and has no teams`() {
        val four = starLobby().removePlayer(4)
        assertEquals(40, four.startingLife)
        assertTrue(!four.canStart)
        assertSame(four, four.start(0, "g", firstSeat))
        assertTrue(starLobby().canStart)
        assertTrue(!starLobby().addPlayer("Fay").canStart)
        // Choosing Star turns Two-Headed Giant off, and it can't be turned back on.
        val fromTeams = teams().addPlayer("Eve").setFormat(GameProfile.MTG_STAR_COMMANDER)
        assertTrue(!fromTeams.twoHeadedGiant)
        assertEquals(10, fromTeams.poisonLimit)
        assertSame(fromTeams, fromTeams.setTwoHeadedGiant(true))
        // Seat order is the order players joined.
        assertEquals(listOf("Ana", "Ben", "Cy", "Dee", "Eve"), star().players.map { it.name })
        assertEquals(List(5) { 40 }, star().players.map { it.life })
    }

    @Test
    fun `allies are neighbours and enemies are across the star for every seat`() {
        val expected = listOf(
            listOf(1, 4) to listOf(2, 3),
            listOf(2, 0) to listOf(3, 4),
            listOf(3, 1) to listOf(4, 0),
            listOf(4, 2) to listOf(0, 1),
            listOf(0, 3) to listOf(1, 2),
        )
        expected.forEachIndexed { seat, (allies, enemies) ->
            assertEquals(allies, StarCommander.allies(seat))
            assertEquals(enemies, StarCommander.enemies(seat))
            // Symmetric, and every other seat is exactly one of the two.
            allies.forEach { assertTrue(StarCommander.allied(it, seat)) }
            enemies.forEach { assertTrue(!StarCommander.allied(it, seat) && seat in StarCommander.enemies(it)) }
            assertEquals((0 until 5).filter { it != seat }.toSet(), (allies + enemies).toSet())
        }
        val summary = star().toSummary(1_000, 0)
        assertEquals(listOf("Ben", "Eve"), summary.starAllies(1).map { it.label })
        assertEquals(listOf("Cy", "Dee"), summary.starEnemies(1).map { it.label })
    }

    @Test
    fun `swapping seats changes relationships in the lobby and they are fixed in the game`() {
        val swapped = starLobby().swapPlayers(1, 2)
        assertEquals(listOf("Cy", "Eve"), swapped.toSummary(0, 0).starAllies(1).map { it.label })
        assertEquals(listOf("Ben", "Dee"), swapped.toSummary(0, 0).starEnemies(1).map { it.label })
        val game = swapped.start(1_000, "s", firstSeat)
        assertSame(game, game.swapPlayers(0, 2))
        assertEquals(listOf("Ana", "Cy", "Ben", "Dee", "Eve"), game.players.map { it.name })
    }

    @Test
    fun `allies can't deal each other commander damage but enemies can and life is never restricted`() {
        val game = star()
        assertSame(game, game.commanderDamage(0, source = 1, commander = 1, delta = 5))
        assertSame(game, game.commanderDamage(0, source = 4, commander = 2, delta = 5))
        val hit = game.commanderDamage(0, source = 2, commander = 1, delta = 5)
        assertEquals(35, hit.players[0].life)
        assertEquals(listOf(5, 0), hit.players[0].commanderDamage[2])
        val summary = game.toSummary(1_000, 0)
        assertTrue(!summary.commanderDamageAllowed(1, 2) && !summary.commanderDamageAllowed(1, 5))
        assertTrue(summary.commanderDamageAllowed(1, 3) && summary.commanderDamageAllowed(1, 4))
        // Ordinary life changes between allies are fine, and can knock an ally out.
        val lost = game.changeLife(1, -40)
        assertTrue(lost.players[1].eliminated)
        assertEquals(TableState.RUNNING, lost.state)
        // Commander damage still works in plain Commander between any two players.
        assertEquals(35, threePlayers().commanderDamage(0, 1, 1, 5).players[0].life)
    }

    @Test
    fun `a player wins once both enemies are out, not after one`() {
        var game = star().changeLife(2, -40)
        assertTrue(game.players[2].eliminated)
        assertEquals(TableState.RUNNING, game.state)
        assertEquals("Enemies out 1/2", "Enemies out ${StarCommander.enemiesOut(0, game.players.map { it.eliminated })}/2")
        game = game.eliminate(3, 5_000)
        assertEquals(TableState.GAME_OVER, game.state)
        assertEquals(0, game.winner)
        assertEquals(listOf(0), game.winners)
        // Three players are still in: Star Commander never waits for the last one standing.
        assertEquals(3, game.players.count { !it.eliminated })
    }

    @Test
    fun `concessions and commander damage count as enemies out`() {
        val conceded = star().concede(2, 2_000).concede(3, 3_000)
        assertEquals(listOf(0), conceded.winners)
        var game = star().concede(2, 2_000)
        repeat(21) { game = game.commanderDamage(3, source = 0, commander = 1, delta = 1) }
        assertEquals(TableState.GAME_OVER, game.state)
        assertEquals(listOf(0), game.winners)
        val poisoned = star().concede(3, 2_000).changeCounter(2, "poison", 10)
        assertEquals(listOf(0), poisoned.winners)
    }

    @Test
    fun `simultaneous eliminations share the victory and nobody left is a draw`() {
        val shared = star().knockOut(2, 3, 4)
        assertEquals(TableState.GAME_OVER, shared.state)
        assertEquals(listOf(0, 1), shared.winners)
        assertEquals(0, shared.winner)
        val summary = shared.toSummary(4_000, 0)
        assertEquals(listOf(1, 2), summary.winnerPlayerNumbers)
        assertTrue(summary.isWinner(1) && summary.isWinner(2) && !summary.isWinner(3))
        assertTrue(!summary.endedInDraw)
        val record = shared.record()!!
        assertTrue(record.won(0) && record.won(1) && !record.won(2) && !record.draw)

        val nobody = star().knockOut(0, 1, 2, 3, 4)
        assertEquals(TableState.GAME_OVER, nobody.state)
        assertEquals(emptyList<Int>(), nobody.winners)
        assertNull(nobody.winner)
        assertTrue(nobody.toSummary(4_000, 0).endedInDraw)
        assertTrue(nobody.record()!!.draw)
    }

    @Test
    fun `players already out never win even when their enemies are gone`() {
        // Dee out, then Ana and Cy together: Ana's enemies (Cy, Dee) are out, but so is she.
        var game = star().eliminate(3, 2_000).knockOut(0, 2)
        assertEquals(TableState.RUNNING, game.state)
        game = game.eliminate(4, 5_000)
        assertEquals(listOf(1), game.winners)
    }

    @Test
    fun `a claimed win still ends a star game with one winner`() {
        val game = star().claimWin(3, 9_000)
        assertEquals(TableState.GAME_OVER, game.state)
        assertEquals(listOf(3), game.winners)
        assertEquals(listOf(4), game.toSummary(9_000, 0).winnerPlayerNumbers)
        assertEquals(listOf(""), star().endWithoutWinner(2_000).record()!!.importFields("THA-A", List(5) { "p$it" })
            .filter { it.first == "winners" }.map { it.second })
    }

    @Test
    fun `rematch and reset keep star and the seats and start over`() {
        val over = star().changeLife(1, -3).knockOut(2, 3, 4)
        val again = over.rematch(10_000, "s2", firstSeat)
        assertEquals(TableState.RUNNING, again.state)
        assertEquals(GameProfile.MTG_STAR_COMMANDER, again.profile)
        assertEquals(listOf("Ana", "Ben", "Cy", "Dee", "Eve"), again.players.map { it.name })
        assertEquals(List(5) { 40 }, again.players.map { it.life })
        assertTrue(again.players.none { it.eliminated })
        assertEquals(emptyList<Int>(), again.winners)
        assertNull(again.winner)
        val lobby = over.reset()
        assertEquals(TableState.LOBBY, lobby.state)
        assertEquals(GameProfile.MTG_STAR_COMMANDER, lobby.profile)
        assertEquals(emptyList<Int>(), lobby.winners)
        assertEquals(listOf("Ana", "Ben", "Cy", "Dee", "Eve"), lobby.players.map { it.name })
    }

    @Test
    fun `records, history and stats credit every winner and survive storage`() {
        val shared = star().knockOut(2, 3, 4)
        val record = shared.record()!!
        assertEquals(listOf(0, 1), record.winners)
        assertEquals(record, GameRecord.fromJson(record.toJson()))
        // Records from before shared victories read their single winner.
        val old = GameRecord.fromJson(record.toJson().apply { remove("winners") })!!
        assertEquals(listOf(0), old.winners)
        val fields = record.importFields("THA-A", List(5) { "p$it" }).toMap()
        assertEquals("0", fields["winner"])
        assertEquals("0,1", fields["winners"])
        assertEquals("mtg_star_commander", fields["gameProfile"])

        val library = LocalLibrary(players = listOf(SavedLocalPlayer("b", "Ben")))
            .withRecord(record).withRecord(threePlayers().claimWin(0, 5_000).record()!!)
        assertEquals(LocalTotals(1, 1, 0), library.totals("a"))
        assertEquals(LocalTotals(1, 1, 0), library.totals("b"))
        assertEquals(LocalTotals(1, 0, 0), library.totals("c"))
        assertEquals(1, library.stats("b").starPlayed)
        assertEquals(1, library.stats("b").starWon)
        assertEquals(0, library.stats("c").starWon)
        assertEquals("Win", library.profileStatistics("b").lastGame.result)
        assertEquals(library, LocalLibrary.fromJson(library.toJson()))

        // The game itself, mid-match and finished, round-trips too.
        val running = star().changeLife(2, -40).commanderDamage(0, 3, 1, 4)
        assertEquals(running, StandaloneTable.gameFromJson(StandaloneTable.gameToJson(running)))
        assertEquals(shared, StandaloneTable.gameFromJson(StandaloneTable.gameToJson(shared)))
        val before = StandaloneTable.gameToJson(star().claimWin(3, 2_000)).apply { remove("winners") }
        assertEquals(listOf(3), StandaloneTable.gameFromJson(before).winners)
    }

    private class MemoryStore : StandaloneStore {
        var game: String? = null
        var records: String? = null
        var library: String? = null
        var failWrites = false
        var writes = 0
        override fun loadLibrary() = library
        override fun saveSnapshot(game: String, library: String): Boolean {
            if (failWrites) return false
            writes++
            this.game = game
            this.library = library
            return true
        }
        override fun loadGame() = game
        override fun loadRecords() = records
    }
}
