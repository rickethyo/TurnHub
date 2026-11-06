package com.turnhub.android.lan

import com.turnhub.android.protocol.GameProfile
import com.turnhub.android.protocol.TableState
import com.turnhub.android.standalone.*
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test
import java.util.UUID

class LanSessionsTest {
    @Test fun `guest everyone controls update all five counters through the real host engine`() {
        val f = Fixture(tcg(GameProfile.MTG_COMMANDER))
        val token = f.join("player", 1).getString("token")
        StandaloneGame.EVERYONE_COUNTERS.forEach { kind ->
            assertEquals(kind, "ok", f.sessions.handle(f.action(token, "everyone:$kind:1", 0)).getString("code"))
            val players = StandaloneTable.gameToJson(f.table.state.value.game).getJSONArray("players")
            val counters = (0 until players.length()).map { players.getJSONObject(it).getJSONObject("counters").getInt(kind) }
            assertEquals(kind, listOf(1, 1), counters)
            assertEquals(kind, "ok", f.sessions.handle(f.action(token, "everyone:$kind:-1", 0)).getString("code"))
        }
        val request = f.action(token, "everyone:rad:1", 0)
        assertEquals("ok", f.sessions.handle(request).getString("code"))
        assertEquals("ok", f.sessions.handle(request).getString("code"))
        assertEquals(listOf(1, 1), f.table.state.value.game.players.map { it.counters.rad })
        assertEquals("stale", f.sessions.handle(f.action(token, "everyone:rad:1", 0).put("match", "old-match")).getString("code"))
        listOf("everyone:rad:2", "everyone:rad:0", "everyone:rad:+1", "everyone:commander-casts:1").forEach {
            assertEquals("rejected", f.sessions.handle(f.action(token, it, 0)).getString("code"))
        }
        listOf("slot", "source", "value").forEach {
            assertEquals("rejected", f.sessions.handle(f.action(token, "everyone:rad:1", 0).put(it, 1)).getString("code"))
        }
        assertEquals(listOf(1, 1), f.table.state.value.game.players.map { it.counters.rad })
    }

    @Test fun `everyone counters respect shared poison living players and read only views`() {
        val teams = Fixture(tcg(GameProfile.MTG_COMMANDER, teams = true))
        val token = teams.join("player", 1).getString("token")
        assertEquals("ok", teams.sessions.handle(teams.action(token, "everyone:poison:1", 0)).getString("code"))
        assertEquals(listOf(1, 1, 1, 1), teams.table.state.value.game.players.map { it.counters.poison })
        val display = teams.join("planes", 0).getString("token")
        assertEquals("forbidden", teams.sessions.handle(teams.action(display, "everyone:rad:1", 0)).getString("code"))
        val nonMagic = Fixture(tcg(GameProfile.POKEMON))
        val pokemon = nonMagic.join("player", 1).getString("token")
        assertEquals("rejected", nonMagic.sessions.handle(nonMagic.action(pokemon, "everyone:rad:1", 0)).getString("code"))

        var game = StandaloneGame().setFormat(GameProfile.MTG_COMMANDER)
        repeat(3) { game = game.addPlayer("Player $it") }
        game = game.chooseStarter(0).start(1_000, "living-match")
        game = game.copy(players = game.players.mapIndexed { i, p -> if (i == 2) p.copy(eliminated = true) else p })
        val living = Fixture(game)
        val live = living.join("player", 1).getString("token")
        val out = living.join("player", 3).getString("token")
        assertEquals("rejected", living.sessions.handle(living.action(out, "everyone:rad:1", 0)).getString("code"))
        assertEquals("ok", living.sessions.handle(living.action(live, "everyone:rad:1", 0)).getString("code"))
        assertEquals(listOf(1, 1, 0), living.table.state.value.game.players.map { it.counters.rad })
    }

    private fun tcg(profile: GameProfile, teams: Boolean = false): StandaloneGame {
        var g = StandaloneGame().setFormat(profile).setTwoHeadedGiant(teams)
        val count = if (profile.starCommander || profile.bagOfDoom) 5 else if (profile.emperor) 6 else if (teams) 4 else 2
        repeat(count) { g = g.addPlayer("Player ${it + 1}", localId = "private-$it") }
        return g.chooseStarter(0).start(1_000, "shared-match")
    }

    @Test fun `TCG players control only their own score and can poll the entire table with the same token`() {
        listOf(GameProfile.GENERIC, GameProfile.MTG, GameProfile.MTG_COMMANDER, GameProfile.MTG_STAR_COMMANDER,
            GameProfile.MTG_BAG_OF_DOOM, GameProfile.MTG_EMPEROR, GameProfile.YUGIOH, GameProfile.POKEMON, GameProfile.LORCANA).forEach { profile ->
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
            assertEquals("invalid", f.join("spectator", 0).getString("code"))
        }
    }

    @Test fun `TCG capabilities refuse host lifecycle rules and foreign profile commands`() {
        val f = Fixture(tcg(GameProfile.MTG_COMMANDER))
        val token = f.join("player").getString("token")
        listOf("pause", "reset", "rematch", "draw", "start", "day", "back-to-setup").forEach {
            assertEquals(it, "rejected", f.sessions.handle(f.action(token, it, 0)).getString("code"))
        }
        assertEquals("rejected", f.sessions.handle(f.action(token, "life", 10_001)).getString("code"))
        assertEquals("invalid", f.sessions.handle(f.action(token, "life").put("source", 1.5)).getString("code"))
        assertEquals(0, f.store.commits)
        assertEquals("ok", Fixture(tcg(GameProfile.GENERIC)).join("player").getString("code"))
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

    private fun game(): StandaloneGame = StandaloneGame().setFormat(GameProfile.MTG)
        .addPlayer("A", localId = "private-a").addPlayer("B", localId = "private-b")
        .chooseStarter(0).start(1_000, "shared-match")
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
    @Test fun `finished guests rematch and reset once while ordinary game over edits stay rejected`() {
        val f = Fixture()
        val token = f.join().getString("token")
        assertEquals("ok", f.sessions.handle(f.action(token, "eliminate", 0)).getString("code"))
        assertEquals(TableState.GAME_OVER, f.table.state.value.game.state)
        assertEquals("rejected", f.sessions.handle(f.action(token, "life", 1)).getString("code"))
        val old = f.table.state.value.game.gameId
        val rematch = f.action(token, "rematch", 0)
        assertEquals("ok", f.sessions.handle(rematch).getString("code"))
        val new = f.table.state.value.game.gameId
        assertNotEquals(old, new)
        assertEquals("ok", f.sessions.handle(rematch).getString("code"))
        assertEquals(new, f.table.state.value.game.gameId)
        assertEquals("rejected", f.sessions.handle(f.action(token, "reset", 0)).getString("code"))
        assertEquals("ok", f.sessions.handle(f.action(token, "eliminate", 0)).getString("code"))
        assertEquals("ok", f.sessions.handle(f.action(token, "reset", 0)).getString("code"))
        assertEquals(TableState.LOBBY, f.table.state.value.game.state)
        assertEquals(2, f.table.state.value.library.history.size)
        assertEquals(2, StandaloneTable(f.store).state.value.library.history.size)
    }

    @Test fun `commander names follow authenticated seat and receipts bind the full card payload`() {
        val f = Fixture(tcg(GameProfile.MTG_COMMANDER))
        val token = f.join(player = 2).getString("token")
        val request = f.action(token, "set-commander", 0).put("player", 1)
            .put("card", LanCard("Atraxa", null, false).json())
        assertEquals("ok", f.sessions.handle(request).getString("code"))
        assertNull(f.table.state.value.game.players[0].commander)
        assertEquals("Atraxa", f.table.state.value.game.players[1].commander)
        assertEquals("ok", f.sessions.handle(request).getString("code"))
        assertEquals("invalid", f.sessions.handle(JSONObject(request.toString())
            .put("card", LanCard("Other commander", null, false).json())).getString("code"))
        assertEquals("invalid", f.sessions.handle(f.action(token, "set-commander", 0)
            .put("card", LanCard("x".repeat(StandaloneGame.MAX_COMMANDER + 1), null, false).json())).getString("code"))
        kotlinx.coroutines.runBlocking { f.table.control(com.turnhub.android.data.TabletSeat(StandaloneGame.seatHandle(0).id, 1), "reset") }
        assertEquals("ok", f.sessions.handle(f.action(token, "set-commander", 0)
            .put("card", LanCard("Lobby commander", null, false).json())).getString("code"))
        assertEquals("Lobby commander", f.table.state.value.game.players[1].commander)
    }

    private inner class Fixture(initial: StandaloneGame = game()) {
        val store = Store(initial)
        val table = StandaloneTable(store, wallClock = { 90_000 }, shortGameMs = 0)
        var unavailable = false
        var now = 0L
        var looks: List<LocalPersonalization?> = emptyList()
        val sessions = LanSessions("session", "invitation-key", { table.state.value.game }, table::applyLanAction,
            unavailable = { unavailable || table.state.value.storageProblem != null }, now = { now }, looks = { looks })
        fun request(op: String) = JSONObject().put("v", LAN_VERSION).put("session", "session").put("op", op)
        fun join(role: String = "player", player: Int = 1, id: String = UUID.randomUUID().toString()) =
            sessions.handle(request("join").put("key", "invitation-key").put("role", role).put("player", player).put("joinId", id))
        fun action(token: String, kind: String = "life", value: Int = 1, id: String = UUID.randomUUID().toString()) =
            request("command").put("token", token).put("match", table.state.value.game.gameId).put("revision", table.state.value.game.revision)
                .put("kind", kind).put("value", value).put("slot", 0).put("actionId", id)
    }

    @Test fun `phone manual planeswalk and roll use real engine once with freshness and argument checks`() {
        val deck = listOf(PlanarCard("plane-a"), PlanarCard("plane-b"), PlanarCard("plane-c"))
        val f = Fixture(StandaloneGame().setFormat(GameProfile.MTG_COMMANDER).setPlanechase(true)
            .addPlayer("A").addPlayer("B").chooseStarter(0).start(1_000, "shared-match", planarDeck = deck))
        val token = f.join("player", 1).getString("token")
        val offTurn = f.join("player", 2).getString("token")
        assertEquals("rejected", f.sessions.handle(f.action(offTurn, "planar-roll", 0)).getString("code"))
        assertEquals("rejected", f.sessions.handle(f.action(offTurn, "planar-roll", 1)).getString("code"))
        val walk = f.action(token, "planar-walk", 0)
        assertEquals("ok", f.sessions.handle(walk).getString("code"))
        assertEquals("plane-b", f.table.state.value.game.planechase.faceUp.single().id)
        assertEquals("ok", f.sessions.handle(walk).getString("code"))
        assertEquals("plane-b", f.table.state.value.game.planechase.faceUp.single().id)
        assertEquals("stale", f.sessions.handle(f.action(token, "planar-walk", 0).put("match", "old")).getString("code"))
        assertEquals("rejected", f.sessions.handle(f.action(token, "planar-walk", 1)).getString("code"))
        assertEquals("rejected", f.sessions.handle(f.action(token, "planar-roll", 0).put("source", 2)).getString("code"))
        assertEquals("ok", f.sessions.handle(f.action(token, "planar-roll", 0)).getString("code"))
        assertNotNull(f.table.state.value.game.planechase.lastRoll)
    }

    @Test fun `a planes display and the seats run the host's planes without seeing its deck`() {
        val deck = listOf(PlanarCard("plane-a"), PlanarCard("plane-b"), PlanarCard("plane-c"))
        val f = Fixture(StandaloneGame().setFormat(GameProfile.MTG_COMMANDER).setPlanechase(true)
            .addPlayer("A").addPlayer("B").chooseStarter(0).start(1_000, "shared-match", planarDeck = deck))
        val joined = f.join("planes", 0)
        assertEquals("ok", joined.getString("code"))
        val planar = joined.getJSONObject("game").getJSONObject("planechase")
        assertEquals(0, planar.getJSONArray("deck").length())
        assertEquals("plane-a", planar.getJSONArray("faceUp").getJSONObject(0).getString("id"))
        val display = joined.getString("token")
        // The display only sends planar actions; they act for the active player.
        assertEquals("forbidden", f.sessions.handle(f.action(display, "life", -1)).getString("code"))
        assertEquals("ok", f.sessions.handle(f.action(display, "planar-counter", 1).put("slot", 1)).getString("code"))
        assertEquals(1, f.table.state.value.game.planechase.counters["plane-a"])
        assertEquals("rejected", f.sessions.handle(f.action(display, "planar-counter", 1).put("slot", 2)).getString("code"))
        assertEquals("ok", f.sessions.handle(f.action(display, "planar-walk", 0)).getString("code"))
        assertEquals("plane-b", f.table.state.value.game.planechase.faceUp.single().id)
        // A seat may press them too, on anyone's turn, like the host's own Planes tab.
        val b = f.join("player", 2).getString("token")
        assertEquals("ok", f.sessions.handle(f.action(b, "planar-chaos", 0)).getString("code"))
        assertEquals(1, f.table.state.value.game.planechase.chaos)
        assertEquals("ok", f.sessions.handle(f.action(display, "planar-roll", 0)).getString("code"))
        assertEquals(0, f.table.state.value.game.planechase.lastRoll?.player)
        // Not without Planechase, and never in other games.
        assertEquals("invalid", Fixture(tcg(GameProfile.POKEMON)).join("planes", 0).getString("code"))
        val off = Fixture(tcg(GameProfile.MTG))
        val offDisplay = off.join("planes", 0).getString("token")
        assertEquals("rejected", off.sessions.handle(off.action(offDisplay, "planar-roll", 0)).getString("code"))
    }

    @Test fun `Bag of Doom guests' life loss makes the turn's owner safe and pauses only for the host's draw`() {
        val f = Fixture(tcg(GameProfile.MTG_BAG_OF_DOOM))
        val owner = f.table.state.value.game.active!!
        val victim = (owner + 1) % 5
        val token = f.join("player", victim + 1).getString("token")
        assertTrue(f.table.state.value.game.doomBag.contains(owner + 1))
        assertEquals("ok", f.sessions.handle(f.action(token, "life", -3)).getString("code"))
        assertFalse(f.table.state.value.game.doomBag.contains(owner + 1))
        assertTrue(f.table.state.value.game.doomBag.contains(victim + 1))
        // The guest sees the bag but can't draw, correct or credit it.
        val view = f.sessions.handle(f.request("poll").put("token", token)).getJSONObject("game")
        assertEquals(0, view.getJSONObject("doomBag").getInt("selected"))
        listOf("draw", "doom", "eligibility", "credit").forEach {
            assertEquals(it, "rejected", f.sessions.handle(f.action(token, it, 0)).getString("code"))
        }
    }

    @Test fun `any seated guest pauses or resumes the table and a repeated tap changes nothing`() {
        val f = Fixture(tcg(GameProfile.MTG))
        val a = f.join("player", 1).getString("token")
        val b = f.join("player", 2).getString("token")
        assertEquals("ok", f.sessions.handle(f.action(a, "pause", 1)).getString("code"))
        assertEquals(TableState.PAUSED, f.table.state.value.game.state)
        // A second Pause (another phone, or a crossed tap) never resumes.
        assertEquals("rejected", f.sessions.handle(f.action(b, "pause", 1)).getString("code"))
        assertEquals(TableState.PAUSED, f.table.state.value.game.state)
        assertEquals("ok", f.sessions.handle(f.action(b, "pause", 0)).getString("code"))
        assertEquals(TableState.RUNNING, f.table.state.value.game.state)
        assertEquals("rejected", f.sessions.handle(f.action(a, "pause", 2)).getString("code"))
    }

    @Test fun `a guest's win claim waits for the other players in turn order and a denial withdraws it`() {
        val f = Fixture(StandaloneGame().setFormat(GameProfile.MTG_COMMANDER)
            .addPlayer("A").addPlayer("B").addPlayer("C").chooseStarter(0).start(1_000, "shared-match"))
        val a = f.join("player", 1).getString("token")
        val b = f.join("player", 2).getString("token")
        val c = f.join("player", 3).getString("token")
        assertEquals("ok", f.sessions.handle(f.action(a, "win", 0)).getString("code"))
        var game = f.table.state.value.game
        assertEquals(TableState.RUNNING, game.state)
        assertEquals(0, game.openWinClaim); assertEquals(1, game.winConfirmer)
        // Only the current confirmer answers; C waits for B.
        assertEquals("rejected", f.sessions.handle(f.action(c, "confirm", 0)).getString("code"))
        assertEquals("ok", f.sessions.handle(f.action(b, "confirm", 0)).getString("code"))
        assertEquals(2, f.table.state.value.game.winConfirmer)
        assertEquals("ok", f.sessions.handle(f.action(c, "deny", 0)).getString("code"))
        game = f.table.state.value.game
        assertNull(game.openWinClaim); assertEquals(TableState.RUNNING, game.state)
        // A second claim, confirmed by both, ends the match for the claimant.
        f.sessions.handle(f.action(a, "win", 0))
        f.sessions.handle(f.action(b, "confirm", 0))
        f.sessions.handle(f.action(c, "confirm", 0))
        game = f.table.state.value.game
        assertEquals(TableState.GAME_OVER, game.state); assertEquals(0, game.winner)
        // The claim survives a restart of the host app while it is open.
        val open = StandaloneGame().setFormat(GameProfile.MTG_COMMANDER).addPlayer("A").addPlayer("B").chooseStarter(0)
            .start(1_000, "m").requestWin(1, 2_000)
        assertEquals(1, open.openWinClaim)
        assertEquals(open.winConfirmer, StandaloneTable.gameFromJson(StandaloneTable.gameToJson(open)).winConfirmer)
        // The last confirmer knocked out: the claim lapses and claiming again wins at once.
        val lapsed = open.eliminate(0, 3_000)
        assertTrue(lapsed.state == TableState.GAME_OVER || lapsed.openWinClaim == null)
    }

    @Test fun `guests receive the seats' looks and commander art, with images only when their version changes`() {
        val f = Fixture(tcg(GameProfile.MTG_COMMANDER).setCommander(0, "Atraxa", "0b2b3b0a-1c2d-4e5f-8a9b-0c1d2e3f4a5b"))
        val image = java.util.Base64.getEncoder().encodeToString(ByteArray(64) { 1 })
        f.looks = listOf(LocalPersonalization(color = "#123456", avatar = "cat",
            tileBackground = com.turnhub.android.domain.TileBackground(com.turnhub.android.domain.TileBackgroundStyle.IMAGE, "#654321", image)), null)
        val joined = f.join("player", 2)
        assertEquals("0b2b3b0a-1c2d-4e5f-8a9b-0c1d2e3f4a5b", StandaloneTable.gameFromJson(joined.getJSONObject("game")).players[0].commanderArt)
        val looks = joined.getJSONArray("looks")
        assertEquals(2, looks.length()); assertTrue(looks.isNull(1))
        val first = LocalPersonalization.fromJson(looks.getJSONObject(0))
        assertEquals("#123456", first.color); assertEquals("cat", first.avatar); assertEquals(image, first.tileBackground.image)
        assertFalse(joined.toString().contains("private-"))
        val id = joined.getString("looksId")
        val token = joined.getString("token")
        val same = f.sessions.handle(f.request("poll").put("token", token).put("looksId", id))
        assertFalse(same.has("looks")); assertEquals(id, same.getString("looksId"))
        f.looks = listOf(null, LocalPersonalization(color = "#000000"))
        val changed = f.sessions.handle(f.request("poll").put("token", token).put("looksId", id))
        assertTrue(changed.getJSONArray("looks").isNull(0))
    }

    @Test fun `invitation and session authentication gate every snapshot`() {
        val f = Fixture()
        val bad = f.sessions.handle(f.request("preview").put("key", "wrong"))
        assertEquals("unauthorized", bad.getString("code")); assertFalse(bad.has("game"))
        assertEquals("invalid", f.sessions.handle(f.request("preview").put("v", LAN_VERSION + 1).put("key", "invitation-key")).getString("code"))
        assertEquals("invalid", f.sessions.handle(f.request("preview").put("session", "old").put("key", "invitation-key")).getString("code"))
        assertFalse(f.sessions.handle(f.request("poll").put("token", "unknown")).has("game"))
    }

    @Test fun `public snapshots carry the game without local identity or library`() {
        val f = Fixture()
        val reply = f.join("planes", 0)
        val json = reply.getJSONObject("game")
        assertFalse(reply.has("library"))
        assertFalse(json.toString().contains("private-a"))
        val decoded = StandaloneTable.gameFromJson(json)
        assertTrue(decoded.players.all { it.localId == null && it.profileId == null })
    }

    @Test fun `display cannot mutate and a player token cannot select another seat or table setting`() {
        val f = Fixture()
        val display = f.join("planes", 0).getString("token")
        assertEquals("forbidden", f.sessions.handle(f.action(display)).getString("code"))
        val own = f.join().getString("token")
        assertEquals("ok", f.sessions.handle(f.action(own).put("player", 2)).getString("code"))
        assertEquals(listOf(21, 20), f.table.state.value.game.players.map { it.life })
        assertEquals(1, f.store.commits)
        val displayUpdate = f.sessions.handle(f.request("poll").put("token", display)).getJSONObject("game")
        assertEquals(21, displayUpdate.getJSONArray("players").getJSONObject(0).getInt("life"))
        assertEquals("rejected", f.sessions.handle(f.action(own, "life", 10_001)).getString("code"))
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
        assertEquals(21, f.table.state.value.game.players[0].life)
        assertEquals(1, f.store.commits)
        assertEquals("invalid", f.sessions.handle(JSONObject(command.toString()).put("value", -1)).getString("code"))
    }

    @Test fun `concurrent stale edits do not overwrite the host or another player`() {
        val f = Fixture()
        val a = f.join().getString("token")
        val b = f.join(player = 2).getString("token")
        val stale = f.action(b)
        assertEquals("ok", f.sessions.handle(f.action(a)).getString("code"))
        val reply = f.sessions.handle(stale)
        assertEquals("stale", reply.getString("code"))
        assertEquals(listOf(21, 20), f.table.state.value.game.players.map { it.life })
        assertEquals(f.table.state.value.game.revision, reply.getJSONObject("game").getLong("revision"))
    }

    @Test fun `failed durable save publishes no mutation or successful acknowledgement`() {
        val f = Fixture()
        val token = f.join().getString("token")
        val before = f.store.saved
        f.store.fail = true
        val reply = f.sessions.handle(f.action(token))
        assertEquals("rejected", reply.getString("code"))
        assertEquals(20, f.table.state.value.game.players[0].life)
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

    @Test fun `a final remote elimination creates history once and retries cannot duplicate it`() {
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
        assertEquals("ok", f.join("planes", 0).getString("code"))
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
