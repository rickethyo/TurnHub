package com.turnhub.android.lan

import com.turnhub.android.domain.InvestigatorCard
import com.turnhub.android.domain.InvestigatorCardSlot
import com.turnhub.android.protocol.GameProfile
import com.turnhub.android.standalone.*
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test
import java.util.UUID

class LanSessionsTest {
    private fun tcg(profile: GameProfile, teams: Boolean = false): StandaloneGame {
        var g = StandaloneGame().setFormat(profile).setTwoHeadedGiant(teams)
        val count = if (profile.starCommander || profile.bagOfDoom) 5 else if (teams) 4 else 2
        repeat(count) { g = g.addPlayer("Player ${it + 1}", localId = "private-$it") }
        return g.chooseStarter(0).start(1_000, "shared-match")
    }

    @Test fun `TCG players control only their own score and can poll the entire table with the same token`() {
        listOf(GameProfile.MTG, GameProfile.MTG_COMMANDER, GameProfile.MTG_STAR_COMMANDER,
            GameProfile.MTG_BAG_OF_DOOM, GameProfile.YUGIOH, GameProfile.POKEMON, GameProfile.LORCANA).forEach { profile ->
            val f = Fixture(tcg(profile))
            val joined = f.join("player", 1)
            assertEquals(profile.name, "ok", joined.getString("code"))
            val token = joined.getString("token")
            val before = f.table.state.value.game.players.map { it.life }
            val delta = if (profile == GameProfile.YUGIOH) -1000 else if (profile.lorcana) 1 else -1
            val request = f.action(token, "life", delta).put("player", 2)
            assertEquals(profile.name, "ok", f.sessions.handle(request).getString("code"))
            assertEquals(before[0] + delta, f.table.state.value.game.players[0].life)
            assertEquals(before[1], f.table.state.value.game.players[1].life)
            assertEquals(profile.name, "ok", f.sessions.handle(request).getString("code"))
            assertEquals(1, f.store.commits)
            repeat(3) {
                val view = f.sessions.handle(f.request("poll").put("token", token))
                assertEquals("ok", view.getString("code"))
                assertEquals(1, view.getInt("player"))
                assertEquals(before.size, view.getJSONObject("game").getJSONArray("players").length())
            }
            assertEquals(1, f.store.commits)
            assertEquals("invalid", f.join("elder", 0).getString("code"))
            assertEquals("invalid", f.join("investigator", 2).getString("code"))
        }
    }

    @Test fun `TCG capabilities refuse host lifecycle rules and foreign profile commands`() {
        val f = Fixture(tcg(GameProfile.MTG_COMMANDER))
        val token = f.join("player").getString("token")
        listOf("pause", "reset", "rematch", "draw", "start", "day", "skill", "wounds", "back-to-setup").forEach {
            assertEquals(it, "rejected", f.sessions.handle(f.action(token, it, 0)).getString("code"))
        }
        assertEquals("rejected", f.sessions.handle(f.action(token, "life", 10_001)).getString("code"))
        assertEquals("invalid", f.sessions.handle(f.action(token, "life").put("source", 1.5)).getString("code"))
        assertEquals(0, f.store.commits)
        assertEquals("invalid", Fixture().join("player").getString("code"))
        assertEquals("ended", Fixture(tcg(GameProfile.GENERIC)).join("player").getString("code"))
    }

    @Test fun `Commander source slot and delta are bound to the receipt and own recipient`() {
        val f = Fixture(tcg(GameProfile.MTG_COMMANDER))
        val token = f.join("player").getString("token")
        val request = f.action(token, "commander", 1).put("slot", 2).put("source", 2)
        assertEquals("ok", f.sessions.handle(request).getString("code"))
        assertEquals(listOf(0, 1), f.table.state.value.game.players[0].commanderDamage[1])
        assertEquals(39, f.table.state.value.game.players[0].life)
        assertEquals(40, f.table.state.value.game.players[1].life)
        assertEquals("ok", f.sessions.handle(request).getString("code"))
        assertEquals("invalid", f.sessions.handle(JSONObject(request.toString()).put("source", 1)).getString("code"))
        assertEquals("rejected", f.sessions.handle(f.action(token, "commander", 1).put("slot", 1).put("source", 1)).getString("code"))
        assertEquals(1, f.store.commits)
        val star = Fixture(tcg(GameProfile.MTG_STAR_COMMANDER))
        val own = star.join("player").getString("token")
        assertEquals("rejected", star.sessions.handle(star.action(own, "commander", 1).put("slot", 1).put("source", 2)).getString("code"))
        assertEquals("ok", star.sessions.handle(star.action(own, "commander", 1).put("slot", 1).put("source", 3)).getString("code"))
    }

    @Test fun `Two Headed Giant keeps personal counters and lets either teammate pass shared life and poison`() {
        val f = Fixture(tcg(GameProfile.MTG_COMMANDER, teams = true))
        val token = f.join("player", 2).getString("token")
        assertEquals("ok", f.sessions.handle(f.action(token, "life", -5)).getString("code"))
        assertEquals(listOf(55, 55, 60, 60), f.table.state.value.game.players.map { it.life })
        assertEquals("ok", f.sessions.handle(f.action(token, "poison", 1)).getString("code"))
        assertEquals(listOf(1, 1, 0, 0), f.table.state.value.game.players.map { it.counters.poison })
        assertEquals("ok", f.sessions.handle(f.action(token, "energy", 1)).getString("code"))
        assertEquals(listOf(0, 1, 0, 0), f.table.state.value.game.players.map { it.counters.energy })
        assertEquals("ok", f.sessions.handle(f.action(token, "pass", 0)).getString("code"))
        assertEquals(2, f.table.state.value.game.active)
        assertEquals("rejected", f.sessions.handle(f.action(token, "pass", 0)).getString("code"))
    }

    @Test fun `Pokemon and Lorcana turn marks toggle and scoring completes history once`() {
        listOf(GameProfile.POKEMON, GameProfile.LORCANA).forEach { profile ->
            val f = Fixture(tcg(profile))
            val token = f.join("player").getString("token")
            val mark = StandaloneGame.ACTION_KIND + com.turnhub.android.domain.TurnAction.of(profile).first().name
            assertEquals("ok", f.sessions.handle(f.action(token, mark, 1)).getString("code"))
            assertEquals("ok", f.sessions.handle(f.action(token, mark, 0)).getString("code"))
            assertTrue(f.table.state.value.game.players[0].turn.turnActions.isEmpty())
            val winning = f.action(token, "life", if (profile.pokemon) -6 else 20)
            assertEquals("ok", f.sessions.handle(winning).getString("code"))
            assertEquals("ok", f.sessions.handle(winning).getString("code"))
            assertEquals(1, f.table.state.value.library.history.size)
        }
    }

    @Test fun `TCG commands reject stale snapshots and failed durable writes`() {
        val f = Fixture(tcg(GameProfile.MTG))
        val first = f.join("player").getString("token")
        val second = f.join("player", 2).getString("token")
        val stale = f.action(second, "life", -1)
        assertEquals("ok", f.sessions.handle(f.action(first, "life", -1)).getString("code"))
        assertEquals("stale", f.sessions.handle(stale).getString("code"))
        f.store.fail = true
        assertEquals("rejected", f.sessions.handle(f.action(second, "life", -1)).getString("code"))
        assertEquals(listOf(19, 20), f.table.state.value.game.players.map { it.life })
        assertEquals("unavailable", f.sessions.handle(f.request("poll").put("token", first)).getString("code"))
    }

    @Test fun `TCG lobby reseating returns the claimed seat and rematches retain access but format changes end sharing`() {
        val f = Fixture(tcg(GameProfile.MTG_COMMANDER))
        val token = f.join("player", 2).getString("token")
        kotlinx.coroutines.runBlocking { f.table.control(com.turnhub.android.data.TabletSeat(StandaloneGame.seatHandle(0).id, 1), "reset") }
        f.table.movePlayer(1, -1)
        assertEquals(1, f.sessions.handle(f.request("poll").put("token", token)).getInt("player"))
        f.table.start()
        assertEquals("ok", f.sessions.handle(f.action(token, "life", -1)).getString("code"))
        kotlinx.coroutines.runBlocking { f.table.control(com.turnhub.android.data.TabletSeat(StandaloneGame.seatHandle(0).id, 1), "reset") }
        f.table.setFormat(GameProfile.POKEMON)
        assertEquals("ended", f.sessions.handle(f.request("poll").put("token", token)).getString("code"))
    }

    private fun game(): StandaloneGame {
        var g = StandaloneGame().setFormat(GameProfile.DEATH_MAY_DIE)
            .addPlayer("A", localId = "private-a").addPlayer("B", localId = "private-b")
        repeat(2) { i -> g = g.setInvestigator(i, "Investigator $i")
            .setInvestigatorCard(i, InvestigatorCardSlot.SANITY_CONDITION, InvestigatorCard("Condition $i", "Resolve this card")) }
        return g.setElderOne("Cthulhu").setEpisode("Episode").chooseStarter(0).start(1_000, "shared-match")
    }
    private class Store(game: StandaloneGame) : StandaloneStore {
        var saved = StandaloneTable.gameToJson(game).toString()
        var library = LocalLibrary().toJson().toString()
        var commits = 0
        var fail = false
        override fun loadGame() = saved
        override fun loadLibrary() = library
        override fun loadRecords(): String? = null
        override fun saveSnapshot(game: String, library: String): Boolean {
            if (fail) return false
            saved = game; this.library = library; return true
        }
        override fun commitSnapshot(game: String, library: String): Boolean { commits++; return saveSnapshot(game, library) }
    }
    private inner class Fixture(initial: StandaloneGame = game()) {
        val store = Store(initial)
        val table = StandaloneTable(store, wallClock = { 90_000 }, shortGameMs = 0)
        var unavailable = false
        var now = 0L
        val sessions = LanSessions("session", "invitation-key", { table.state.value.game }, table::applyLanAction,
            unavailable = { unavailable || table.state.value.storageProblem != null }, now = { now })
        fun request(op: String) = JSONObject().put("v", LAN_VERSION).put("session", "session").put("op", op)
        fun join(role: String = "investigator", player: Int = 1, id: String = UUID.randomUUID().toString()) =
            sessions.handle(request("join").put("key", "invitation-key").put("role", role).put("player", player).put("joinId", id))
        fun action(token: String, kind: String = "wounds", value: Int = 1, id: String = UUID.randomUUID().toString()) =
            request("command").put("token", token).put("match", table.state.value.game.gameId).put("revision", table.state.value.game.revision)
                .put("kind", kind).put("value", value).put("slot", 0).put("actionId", id)
    }

    @Test fun `invitation and session authentication gate every snapshot`() {
        val f = Fixture()
        val bad = f.sessions.handle(f.request("preview").put("key", "wrong"))
        assertEquals("unauthorized", bad.getString("code")); assertFalse(bad.has("game"))
        assertEquals("invalid", f.sessions.handle(f.request("preview").put("v", 2).put("key", "invitation-key")).getString("code"))
        assertEquals("invalid", f.sessions.handle(f.request("preview").put("session", "old").put("key", "invitation-key")).getString("code"))
        assertFalse(f.sessions.handle(f.request("poll").put("token", "unknown")).has("game"))
    }

    @Test fun `public snapshots include current elder and cards without local identity or library`() {
        val f = Fixture()
        val reply = f.join("elder", 0)
        val json = reply.getJSONObject("game")
        assertEquals("Cthulhu", json.getJSONObject("elder").getString("elderOne"))
        assertFalse(reply.has("library"))
        assertFalse(json.toString().contains("private-a"))
        val decoded = StandaloneTable.gameFromJson(json)
        assertEquals("Condition 0", decoded.players[0].dashboard.sanityCondition?.name)
        assertTrue(decoded.players.all { it.localId == null && it.profileId == null })
    }

    @Test fun `display cannot mutate and investigator token cannot select another seat or table setting`() {
        val f = Fixture()
        val display = f.join("elder", 0).getString("token")
        assertEquals("forbidden", f.sessions.handle(f.action(display)).getString("code"))
        val own = f.join().getString("token")
        assertEquals("ok", f.sessions.handle(f.action(own).put("player", 2)).getString("code"))
        assertEquals(listOf(1, 0), f.table.state.value.game.players.map { it.life })
        assertEquals(1, f.store.commits)
        f.table.changeElder { it.advance(1) }
        val displayUpdate = f.sessions.handle(f.request("poll").put("token", display)).getJSONObject("game")
        assertEquals(1, displayUpdate.getJSONArray("players").getJSONObject(0).getInt("life"))
        assertEquals(2, displayUpdate.getJSONObject("elder").getInt("space"))
        assertEquals("rejected", f.sessions.handle(f.action(own, "win")).getString("code"))
        assertEquals("rejected", f.sessions.handle(f.action(own, "wounds", 20)).getString("code"))
    }

    @Test fun `lost join and command replies retry without stealing a second seat or applying twice`() {
        val f = Fixture()
        val joinId = UUID.randomUUID().toString()
        val first = f.join(id = joinId).getString("token")
        assertEquals(first, f.join(id = joinId).getString("token"))
        assertEquals("claimed", f.join().getString("code"))
        val command = f.action(first)
        assertEquals("ok", f.sessions.handle(command).getString("code"))
        assertEquals("ok", f.sessions.handle(command).getString("code"))
        assertEquals(1, f.table.state.value.game.players[0].life)
        assertEquals(1, f.store.commits)
        assertEquals("invalid", f.sessions.handle(JSONObject(command.toString()).put("value", -1)).getString("code"))
    }

    @Test fun `concurrent stale edits do not overwrite the host or another investigator`() {
        val f = Fixture()
        val a = f.join().getString("token")
        val b = f.join(player = 2).getString("token")
        val stale = f.action(b)
        assertEquals("ok", f.sessions.handle(f.action(a)).getString("code"))
        val reply = f.sessions.handle(stale)
        assertEquals("stale", reply.getString("code"))
        assertEquals(listOf(1, 0), f.table.state.value.game.players.map { it.life })
        assertEquals(f.table.state.value.game.revision, reply.getJSONObject("game").getLong("revision"))
    }

    @Test fun `failed durable save publishes no mutation or successful acknowledgement`() {
        val f = Fixture()
        val token = f.join().getString("token")
        val before = f.store.saved
        f.store.fail = true
        val reply = f.sessions.handle(f.action(token))
        assertEquals("rejected", reply.getString("code"))
        assertEquals(0, f.table.state.value.game.players[0].life)
        assertEquals(before, f.store.saved)
        assertNotNull(f.table.state.value.storageProblem)
        assertEquals("unavailable", f.sessions.handle(f.request("poll").put("token", token)).getString("code"))
    }

    @Test fun `host disappearance and revocation disable edits without promoting a client`() {
        val f = Fixture()
        val token = f.join().getString("token")
        f.unavailable = true
        assertEquals("unavailable", f.sessions.handle(f.action(token)).getString("code"))
        assertEquals(0, f.store.commits)
        f.unavailable = false
        assertEquals("ok", f.sessions.handle(f.request("poll").put("token", token)).getString("code"))
        f.sessions.revoke(token)
        assertEquals("unauthorized", f.sessions.handle(f.action(token)).getString("code"))
    }

    @Test fun `idle seats release on rejoin and old credentials stop working`() {
        val f = Fixture()
        val old = f.join().getString("token")
        f.now = 5 * 60_000L + 1
        assertEquals("ok", f.join().getString("code"))
        assertEquals("unauthorized", f.sessions.handle(f.request("poll").put("token", old)).getString("code"))
    }

    @Test fun `a final remote death creates history once and retries cannot duplicate it`() {
        val f = Fixture()
        val token = f.join().getString("token")
        val command = f.action(token, "eliminate", 0)
        assertEquals("ok", f.sessions.handle(command).getString("code"))
        assertEquals("ok", f.sessions.handle(command).getString("code"))
        assertEquals(1, f.table.state.value.library.history.size)
        assertEquals(1, f.store.commits)
        val otherMatch = f.action(token).put("match", "different-match")
        assertEquals("stale", f.sessions.handle(otherMatch).getString("code"))
    }

    @Test fun `wire integer fields reject fractions and strings`() {
        val f = Fixture()
        val token = f.join().getString("token")
        assertEquals("invalid", f.sessions.handle(f.action(token).put("value", 1.5)).getString("code"))
        assertEquals("invalid", f.sessions.handle(f.action(token).put("revision", "1")).getString("code"))
        assertEquals(0, f.store.commits)
    }

    @Test fun `investigator markers skills and explicit turn handoff use the host engine`() {
        val f = Fixture()
        val a = f.join().getString("token")
        val b = f.join(player = 2).getString("token")
        listOf("stress", "fire", "insanity").forEach { kind ->
            assertEquals("ok", f.sessions.handle(f.action(a, kind)).getString("code"))
        }
        assertEquals("ok", f.sessions.handle(f.action(a, "skill", 2).put("slot", 1)).getString("code"))
        val player = f.table.state.value.game.players[0]
        assertEquals(1, player.stress); assertEquals(1, player.dashboard.fire)
        assertEquals(1, player.dashboard.insanity); assertEquals(2, player.dashboard.skillLevels[0])
        assertEquals("ok", f.sessions.handle(f.action(b, "take-turn", 0)).getString("code"))
        assertEquals(1, f.table.state.value.game.stagedNextTurn)
        assertEquals("rejected", f.sessions.handle(f.action(b, "pass", 1)).getString("code"))
        assertEquals("ok", f.sessions.handle(f.action(a, "pass", 2)).getString("code"))
        assertEquals(1, f.table.state.value.game.active)
        assertEquals("ok", f.sessions.handle(f.action(b, "pass", 1)).getString("code"))
        assertEquals(0, f.table.state.value.game.active)
        assertEquals(2, f.table.state.value.game.round)
    }

    @Test fun `sharing lasts from the lobby through rematches and follows players by name`() {
        val f = Fixture()
        val token = f.join(player = 2).getString("token")
        kotlinx.coroutines.runBlocking {
            f.table.control(com.turnhub.android.data.TabletSeat(StandaloneGame.seatHandle(0).id, 1), "reset")
        }
        assertEquals(com.turnhub.android.protocol.TableState.LOBBY, f.table.state.value.game.state)
        assertEquals("ok", f.sessions.handle(f.request("poll").put("token", token)).getString("code"))
        val commits = f.store.commits
        assertNotEquals("ok", f.sessions.handle(f.action(token)).getString("code"))
        assertEquals(commits, f.store.commits)
        f.table.removePlayer(0)
        assertEquals(1, f.sessions.guests.single().player)
        assertEquals("ok", f.join("elder", 0).getString("code"))
        f.table.removePlayer(0)
        assertEquals("released", f.sessions.handle(f.request("poll").put("token", token)).getString("code"))
        assertEquals("unauthorized", f.sessions.handle(f.request("poll").put("token", token)).getString("code"))
    }

    @Test fun `ten wrong join codes in a minute lock joining even with the right code`() {
        val f = Fixture()
        repeat(9) { assertEquals("unauthorized", f.sessions.handle(f.request("preview").put("key", "abc-12$it")).getString("code")) }
        assertEquals("locked", f.sessions.handle(f.request("preview").put("key", "zzz-999")).getString("code"))
        assertEquals("locked", f.join().getString("code"))
        f.now = 60_001
        assertEquals("ok", f.join().getString("code"))
    }

    @Test fun `join codes read the same however they are typed`() {
        assertEquals("abc-123", LanJoinCode.normalize(" ABC 123 "))
        assertEquals("abc-123", LanJoinCode.normalize("abc123"))
        assertNull(LanJoinCode.normalize("abl-123"))
        assertNull(LanJoinCode.normalize("abc-12"))
        repeat(50) { assertNotNull(LanJoinCode.normalize(LanJoinCode.generate())) }
    }

    @Test fun `receipt budget never evicts an uncertain acknowledged command`() {
        val f = Fixture()
        val token = f.join().getString("token")
        val original = f.action(token)
        assertEquals("ok", f.sessions.handle(original).getString("code"))
        repeat(2047) { index ->
            assertEquals("ok", f.sessions.handle(f.action(token, value = if (index % 2 == 0) -1 else 1)).getString("code"))
        }
        assertEquals("renew", f.sessions.handle(f.action(token)).getString("code"))
        val commits = f.store.commits
        assertEquals("ok", f.sessions.handle(original).getString("code"))
        assertEquals(commits, f.store.commits)
    }
}
