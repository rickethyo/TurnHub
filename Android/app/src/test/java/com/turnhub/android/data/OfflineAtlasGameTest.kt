package com.turnhub.android.data

import com.turnhub.android.protocol.GameProfile
import com.turnhub.android.protocol.TableState
import com.turnhub.android.standalone.NativeGameCore
import com.turnhub.android.standalone.StandaloneGame
import com.turnhub.android.standalone.StandaloneTable
import kotlinx.coroutines.test.runTest
import org.json.JSONArray
import org.json.JSONObject
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/** Offline play: the game goes on on the phone and every move is queued on Atlas's clock. */
class OfflineAtlasGameTest {
    private val atlasStart = 4_294_967_296L

    /** `GET /api/v1/game` for a Commander game that started on Atlas's clock 10 s ago. */
    private fun body(): String {
        val game = (1..3).fold(StandaloneGame().setFormat(GameProfile.MTG_COMMANDER)) { g, i -> g.addPlayer("Player $i") }
            .chooseStarter(0).start(atlasStart, "atlas-match")
        val state = StandaloneTable.gameToJson(game).put("profile", game.profile.name)
        return JSONObject().put("atlasId", "THA-TEST").put("bootId", "boot-1").put("game", 1)
            .put("atlasMs", atlasStart + 10_000).put("state", state).toString()
    }


    @Test fun `the base gives the phone clock's offset from Atlas's`() {
        val base = OfflineBase.parse(body(), 1_800_000_000_000L)
        assertNotNull(base)
        base!!
        assertEquals("atlas-match", base.gameId)
        assertTrue(base.playing)
        assertEquals(1_800_000_000_000L - atlasStart - 10_000, base.clockOffset)
        assertNull(OfflineBase.parse("{\"atlasId\":\"x\"}", 0))
    }

    @Test fun `moves are queued with Atlas's time and the game goes on locally`() = runTest {
        var now = 1_800_000_000_000L
        val base = OfflineBase.parse(body(), now)!!
        val played = OfflineAtlasGame(base, wallClock = { now }, device = "phone-a")
        played.listen()
        val local = played.table.state.value.game
        assertEquals(TableState.RUNNING, local.state)
        // Moved onto the phone's clock: still 10 s into the game.
        assertEquals(10_000L, local.gameElapsedMs(now))

        now += 3_000
        assertTrue(played.table.control(TabletSeat(8, 1), "pass"))
        now += 1_000
        assertTrue(played.table.life(TabletSeat(10, 1), -4))
        played.table.tick()  // Timeouts are Atlas's to work out again.
        assertEquals(1, played.table.state.value.game.active)
        assertEquals(36, played.table.state.value.game.players[2].life)

        val queue = played.queue
        assertEquals(listOf(1L, 2L), queue.map { it.seq })
        assertEquals(listOf("pass", "changeLife"), queue.map { it.action.getString("do") })
        assertEquals(listOf(atlasStart + 13_000, atlasStart + 14_000), queue.map { it.atMs })
        assertTrue(queue.none { it.action.has("nowMs") })

        val fields = played.syncFields().toMap()
        assertEquals("phone-a", fields["device"])
        assertEquals("boot-1", fields["baseBoot"])
        assertEquals("${atlasStart + 14_000}", fields["nowMs"])
        assertEquals(2, JSONArray(fields["entries"]).length())
        assertEquals("atlas-match", JSONObject(fields["base"]!!).getString("gameId"))

        played.stop()
        played.table.life(TabletSeat(10, 1), -1)
        assertEquals(2, played.queue.size)
    }
    @Test fun `offline journals survive process recreation with game state and stream identity`() = runTest {
        var now = 1_800_000_000_000L
        val store = MemoryOfflineQueueStore()
        val first = OfflineAtlasGame(OfflineBase.parse(body(), now)!!, { now }, "persistent-phone", store::save)
        assertTrue(first.save())
        first.listen()
        assertTrue(first.table.life(TabletSeat(8, 1), -3))
        first.stop()
        now += 2_000
        val restored = OfflineAtlasGame.restore(store.load()!!, store::save, { now })
        assertEquals("persistent-phone", restored.device)
        assertEquals(37, restored.table.state.value.game.players[0].life)
        assertEquals(1, restored.queue.size)
        restored.listen()
        assertTrue(restored.table.life(TabletSeat(8, 1), -2))
        assertEquals(listOf(1L, 2L), restored.queue.map { it.seq })
        assertEquals(35, restored.table.state.value.game.players[0].life)
        assertTrue(restored.acknowledge())
        assertNull(store.load())
    }
    @Test fun `two copies of the same match never record each other's actions`() = runTest {
        val now = 1_800_000_000_000L
        val base = OfflineBase.parse(body(), now)!!
        val one = OfflineAtlasGame(base, { now }, "one")
        val two = OfflineAtlasGame(base, { now }, "two")
        one.listen(); two.listen()
        one.table.life(TabletSeat(8, 1), -2)
        assertEquals(1, one.queue.size)
        assertTrue(two.queue.isEmpty())
        two.table.life(TabletSeat(9, 1), -3)
        assertEquals(1, one.queue.size)
        assertEquals(1, two.queue.size)
    }
    @Test fun `failed journal writes do not apply unrecorded moves and stopped games stay frozen`() = runTest {
        val now = 1_800_000_000_000L
        val played = OfflineAtlasGame(OfflineBase.parse(body(), now)!!, { now }, persist = { false })
        played.listen()
        org.junit.Assert.assertFalse(played.table.life(TabletSeat(8, 1), -2))
        assertEquals(40, played.table.state.value.game.players[0].life)
        assertTrue(played.queue.isEmpty())
        assertNotNull(played.problem.value)
        played.stop()
        org.junit.Assert.assertFalse(played.table.life(TabletSeat(8, 1), -1))
    }
    @Test fun `saved play matches one Atlas table and match across boot changes`() {
        val base = OfflineBase.parse(body(), 1_800_000_000_000L)!!
        val summary = com.turnhub.android.domain.TableSummaryMapper.map(com.turnhub.android.testing.Fixtures.info(),
            com.turnhub.android.testing.Fixtures.state("running.response.json"))
            .copy(atlasId = base.atlasId, game = base.game, matchId = base.gameId)
        assertTrue(base.matches(summary))
        assertTrue(base.matches(summary.copy(bootId = "rebooted")))
        org.junit.Assert.assertFalse(base.matches(summary.copy(matchId = "next-game")))
        org.junit.Assert.assertFalse(base.matches(summary.copy(game = 2)))
        org.junit.Assert.assertFalse(base.matches(summary.copy(atlasId = "other")))
    }

}
