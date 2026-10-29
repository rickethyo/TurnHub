package com.turnhub.android.lan

import com.turnhub.android.data.OfflineChange
import com.turnhub.android.data.TableControls
import com.turnhub.android.data.TabletSeat
import com.turnhub.android.domain.TableSummary
import com.turnhub.android.domain.TableSummaryMapper
import com.turnhub.android.protocol.DoomAction
import com.turnhub.android.protocol.GameActionToken
import com.turnhub.android.protocol.TableState
import com.turnhub.android.testing.Fixtures
import kotlinx.coroutines.runBlocking
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test
import java.util.UUID

class LanAtlasSessionsTest {
    private fun summary(name: String = "running.response.json"): TableSummary {
        val wire = Fixtures.text(name)
        return TableSummaryMapper.map(Fixtures.info(), Fixtures.state(name).copy(wire = wire), receivedAtMs = 1_000)
    }

    /** Records what the hub would send to Atlas through its tablet grant. */
    private class Recorder : TableControls {
        val sent = mutableListOf<String>()
        override suspend fun control(seat: TabletSeat, action: String): Boolean { sent += "control $seat $action"; return true }
        override suspend fun eliminate(seat: TabletSeat, eliminatedBy: Int?): Boolean { sent += "eliminate $seat"; return true }
        override suspend fun resolveElimination(seat: TabletSeat, victim: Int, source: Int?, token: GameActionToken) = true
        override suspend fun doom(seat: TabletSeat, action: DoomAction, token: GameActionToken, player: Int, value: Int) = true
        override suspend fun life(seat: TabletSeat, delta: Int): Boolean { sent += "life $seat $delta"; return true }
        override suspend fun commander(seat: TabletSeat, source: Int, commander: Int, delta: Int) { sent += "commander $seat $source $commander $delta" }
        override suspend fun counter(seat: TabletSeat, kind: String, value: Int, commander: Int) { sent += "counter $seat $kind $value" }
        override suspend fun setEliminationRule(seat: TabletSeat, field: String, value: Int) = Unit
        override suspend fun setAutoEliminateAtZeroLife(seat: TabletSeat, enabled: Boolean) = Unit
        override suspend fun respondLife(seat: TabletSeat, requestId: Long, accept: Boolean) = Unit
        override fun queueOffline(change: OfflineChange) = Unit
        override fun refuseOffline() = Unit
        override suspend fun disable() = Unit
    }

    private inner class Fixture(var table: TableSummary? = summary(), var tablet: Boolean = true) {
        val atlas = Recorder()
        val link = object : LanAtlasLink {
            override fun summary() = table
            override fun canAct() = tablet
            override suspend fun act(game: Int, block: suspend TableControls.() -> Boolean) = atlas.block()
        }
        val sessions = LanAtlasSessions("session", "invitation-key", link, now = { 0 }, clock = { 1_500 })
        fun request(op: String) = JSONObject().put("v", LAN_VERSION).put("session", "session").put("op", op)
        fun handle(body: JSONObject) = runBlocking { sessions.handle(body) }
        fun join(role: String = "player", player: Int = 1) = handle(request("join").put("key", "invitation-key")
            .put("role", role).put("player", player).put("joinId", UUID.randomUUID().toString()))
        fun action(token: String, kind: String, value: Int = 0, slot: Int = 0, source: Int = 0, id: String = UUID.randomUUID().toString()) =
            request("command").put("token", token).put("actionId", id).put("match", table!!.relayMatch()).put("revision", table!!.revision)
                .put("kind", kind).put("value", value).put("slot", slot).put("source", source)
    }

    @Test fun `guests get Atlas's own state, parsed the way a direct connection parses it`() {
        val f = Fixture()
        val joined = f.join()
        assertEquals("ok", joined.getString("code"))
        val relayed = LanAtlasFrame.decode(joined.getJSONObject("atlas"), nowMs = 10_000)
        val direct = f.table!!
        assertEquals(direct.copy(receivedAtMs = 0), relayed.copy(receivedAtMs = 0))
        // Its clocks run from when Atlas sent it, not from when the guest got it.
        assertEquals(10_000 - 500L, relayed.receivedAtMs)
        assertFalse(joined.has("game"))
    }

    @Test fun `a guest's taps go to Atlas for their own seat only`() {
        val f = Fixture()
        val token = f.join(player = 2).getString("token")
        val seat = f.table!!.players.first { it.playerNumber == 2 }.let { TabletSeat(it.controller.id, it.slot) }
        assertEquals("ok", f.handle(f.action(token, "life", -3).put("player", 1)).getString("code"))
        assertEquals("ok", f.handle(f.action(token, "commander", 1, slot = 1, source = 1)).getString("code"))
        assertEquals("ok", f.handle(f.action(token, "pass")).getString("code"))
        assertEquals(listOf("life $seat -3", "commander $seat 1 1 1", "control $seat pass"), f.atlas.sent)
        // Nothing Atlas doesn't take from a seat, and never another table's game.
        assertEquals("rejected", f.handle(f.action(token, "planar-roll")).getString("code"))
        assertEquals("rejected", f.handle(f.action(token, "reset")).getString("code"))
        assertEquals("stale", f.handle(f.action(token, "life", -1).put("match", "other/boot/1")).getString("code"))
        assertEquals(3, f.atlas.sent.size)
    }

    @Test fun `a retried tap is answered from its receipt instead of reaching Atlas twice`() {
        val f = Fixture()
        val token = f.join().getString("token")
        val id = UUID.randomUUID().toString()
        assertEquals("ok", f.handle(f.action(token, "life", -1, id = id)).getString("code"))
        assertEquals("ok", f.handle(f.action(token, "life", -1, id = id)).getString("code"))
        assertEquals("invalid", f.handle(f.action(token, "life", -2, id = id)).getString("code"))
        assertEquals(1, f.atlas.sent.size)
    }

    @Test fun `pause states what it wants, so crossed taps never undo each other`() {
        val f = Fixture()
        val token = f.join().getString("token")
        assertEquals(TableState.RUNNING, f.table!!.state)
        assertEquals("rejected", f.handle(f.action(token, "pause", 0)).getString("code"))
        assertEquals("ok", f.handle(f.action(token, "pause", 1)).getString("code"))
        assertEquals(1, f.atlas.sent.size)
    }

    @Test fun `displays and hubs without the tablet grant only watch`() {
        val f = Fixture(tablet = false)
        val display = f.join("display", 0)
        assertEquals("ok", display.getString("code"))
        assertTrue(display.getBoolean("viewOnly"))
        assertEquals("forbidden", f.handle(f.action(display.getString("token"), "life", -1)).getString("code"))
        val seat = f.join().getString("token")
        assertEquals("view-only", f.handle(f.action(seat, "life", -1)).getString("code"))
        assertTrue(f.atlas.sent.isEmpty())
    }

    @Test fun `the wrong key, a claimed seat and another game are refused`() {
        val f = Fixture()
        assertEquals("unauthorized", f.handle(f.request("preview").put("key", "nope")).getString("code"))
        assertEquals("ok", f.join(player = 1).getString("code"))
        assertEquals("claimed", f.join(player = 1).getString("code"))
        assertEquals("invalid", f.join(player = 9).getString("code"))
        // The hub moved to another game: this invitation was for the first one.
        f.table = f.table!!.copy(game = 2)
        assertEquals("ended", f.handle(f.request("preview").put("key", "invitation-key")).getString("code"))
        // Atlas not answering for a moment is not the end of the session.
        val g = Fixture()
        g.table = null
        assertEquals("unavailable", g.handle(g.request("preview").put("key", "invitation-key")).getString("code"))
    }
}
