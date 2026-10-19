package com.turnhub.android.standalone

import com.turnhub.android.domain.DeathMayDieStory
import com.turnhub.android.domain.ElderOneTrack
import com.turnhub.android.domain.EpisodeToken
import com.turnhub.android.domain.InvestigatorDashboard
import com.turnhub.android.domain.InvestigatorCard
import com.turnhub.android.domain.InvestigatorCardSlot
import com.turnhub.android.protocol.GameProfile
import com.turnhub.android.protocol.TableState
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertSame
import org.junit.Assert.assertTrue
import org.junit.Test
import kotlin.random.Random

class ElderOneTest {
    private fun lobby(players: Int = 2) =
        (1..players).fold(StandaloneGame().setFormat(GameProfile.DEATH_MAY_DIE)) { g, i -> g.addPlayer("P$i") }

    private fun ready(g: StandaloneGame) = g.players.indices.fold(g) { next, i ->
        next.setInvestigator(i, "Investigator $i").setInvestigatorCard(i, InvestigatorCardSlot.SANITY_CONDITION, InvestigatorCard("Condition $i"))
    }

    private fun game(players: Int = 2) = ready(lobby(players)).chooseStarter(0).start(0, "d", Random(0))

    @Test fun `start waits for every investigator, sanity condition and enabled relic`() {
        val setup = lobby()
        assertFalse(setup.canStart)
        assertEquals(listOf("P1" to listOf("investigator", "sanity condition"), "P2" to listOf("investigator", "sanity condition")),
            setup.setupMissing)
        // A sanity condition alone isn't enough: everyone also needs an investigator.
        val conditions = setup.players.indices.fold(setup) { g, i ->
            g.setInvestigatorCard(i, InvestigatorCardSlot.SANITY_CONDITION, InvestigatorCard("Condition $i"))
        }
        assertFalse(conditions.canStart)
        assertSame(conditions, conditions.start(0, "blocked"))
        // Choosing an investigator starts their dashboard afresh, so it comes first.
        val one = setup.setInvestigator(0, "Rasputin").setInvestigatorCard(0, InvestigatorCardSlot.SANITY_CONDITION, InvestigatorCard("Condition"))
        assertEquals(listOf("P2" to listOf("investigator", "sanity condition")), one.setupMissing)
        val readyGame = ready(setup)
        assertTrue(readyGame.canStart)
        assertTrue(readyGame.setupMissing.isEmpty())
        val relics = readyGame.setUseRelics(true)
        assertFalse(relics.canStart)
        assertEquals(listOf("relic"), relics.setupMissing.first().second)
        val all = relics.players.indices.fold(relics) { g, i -> g.setInvestigatorCard(i, InvestigatorCardSlot.RELIC, InvestigatorCard("Relic $i")) }
        assertTrue(all.canStart)
        assertEquals(TableState.RUNNING, all.start(0, "go", Random(0)).state)
        // Other games never ask.
        assertTrue(StandaloneGame().addPlayer("A").addPlayer("B").setupMissing.isEmpty())
    }

    @Test fun `the Elder One and episode are chosen in setup or play and survive a rematch`() {
        val chosen = lobby().setElderOne(" Hastur ").setEpisode("Blasphemous Alchemy")
        assertEquals("Hastur", chosen.elder.elderOne)
        assertEquals("Blasphemous Alchemy", chosen.elder.episode)
        assertNull(chosen.setElderOne(" ").elder.elderOne)
        val magic = StandaloneGame()
        assertSame(magic, magic.setElderOne("Cthulhu"))
        val running = ready(chosen).chooseStarter(0).start(0, "d", Random(0)).changeElder(10) { it.advance(1) }
        assertEquals(2, running.elder.space)
        val rematch = running.claimWin(0, 20).rematch(30, "e", Random(0))
        assertEquals(ElderOneTrack(elderOne = "Hastur", episode = "Blasphemous Alchemy"), rematch.elder)
        assertEquals("Hastur", running.reset().elder.elderOne)
        assertEquals(1, running.reset().elder.space)
    }

    @Test fun `three Summoning symbols advance it and the first red space summons it`() {
        var t = ElderOneTrack()
        assertSame(t, t.symbol(-1))
        t = t.symbol(1).symbol(1)
        assertEquals(2, t.symbols)
        t = t.symbol(1)
        assertEquals(0, t.symbols)
        assertEquals(2, t.space)
        t = t.advance(1).advance(1)
        assertEquals(4, t.space)
        assertFalse(t.summoned)
        assertEquals(1, t.stage)
        t = t.advance(1)
        assertTrue(t.summoned)
        assertEquals(2, t.stage)
        // Moving back is a correction: it stays on the board.
        assertTrue(t.advance(-1).summoned)
        val start = ElderOneTrack()
        assertSame(start, start.advance(-1))
    }

    @Test fun `the ritual summons it and makes it mortal, stage by stage`() {
        var t = ElderOneTrack(elderOne = "Hastur")
        assertSame(t, t.wound(1))
        t = t.disrupt()
        assertTrue(t.summoned)
        assertTrue(t.vulnerable)
        assertEquals(2, t.stage)
        t = t.withHealth(12).wound(9)
        assertFalse(t.stageBeaten)
        t = t.wound(5)
        assertTrue(t.stageBeaten)
        assertEquals("Hastur · Stage 2 · 14/12 wounds", t.headline())
        // Excess wounds don't carry over.
        t = t.defeatStage()
        assertEquals(3, t.stage)
        assertEquals(0, t.stageWounds)
        assertNull(t.health)
        t = t.defeatStage()
        assertEquals(ElderOneTrack.FINAL_STAGE, t.stage)
        assertEquals("Hastur · Final stage · 0 wounds", t.headline())
        assertSame(t, t.defeatStage())
        assertSame(t, t.withHealth(0))
        assertFalse(t.undisrupt().ritualDisrupted)
        val wounded = t.wound(1)
        assertSame(wounded, wounded.undisrupt())
    }

    @Test fun `summoned on the track it can't be wounded until the ritual is disrupted`() {
        val onBoard = ElderOneTrack().advance(1).advance(1).advance(1).advance(1)
        assertTrue(onBoard.summoned)
        assertFalse(onBoard.vulnerable)
        assertSame(onBoard, onBoard.wound(3))
        assertEquals(ElderOneTrack(space = 5), onBoard.unsummon())
        assertTrue(onBoard.disrupt().vulnerable)
    }

    @Test fun `the Progression token reaching the last space loses the game`() {
        val g = game().changeElder(0) { it.copy(space = 7, summoned = true, stage = 2) }
        assertEquals(TableState.RUNNING, g.state)
        val lost = g.changeElder(100) { it.advance(1) }
        assertEquals(TableState.GAME_OVER, lost.state)
        assertTrue(lost.winners.isEmpty())
        assertSame(lost, lost.changeElder(200) { it.advance(-1) })
        // The third symbol on space 7 does the same.
        val symbols = game().changeElder(0) { it.copy(space = 7, summoned = true, stage = 2, symbols = 2) }.changeElder(10) { it.symbol(1) }
        assertEquals(TableState.GAME_OVER, symbols.state)
    }

    @Test fun `killing the Final stage wins for everyone`() {
        val g = game(3).changeElder(0) { it.disrupt() }
        assertSame(g, g.killElderOne(10))
        val final = g.changeElder(5) { it.defeatStage().defeatStage() }
        val won = final.killElderOne(10)
        assertEquals(TableState.GAME_OVER, won.state)
        assertEquals(listOf(0, 1, 2), won.winners)
    }

    @Test fun `a fallen investigator loses the game only while a tracked Elder One is on the track`() {
        // Not tracked here: the table decides, as before.
        assertEquals(TableState.RUNNING, game(3).eliminate(1, 10).state)
        val tracked = game(3).setElderOne("Cthulhu")
        val lost = tracked.eliminate(1, 10)
        assertEquals(TableState.GAME_OVER, lost.state)
        assertTrue(lost.winners.isEmpty())
        val summoned = tracked.changeElder(5) { it.disrupt() }.eliminate(1, 10)
        assertEquals(TableState.RUNNING, summoned.state)
    }

    @Test fun `the track is saved, summarised and checked when restored`() {
        val g = game().setElderOne("Hastur").setEpisode("Tomes of Madness")
            .changeElder(5) { it.symbol(1).disrupt().withHealth(12).wound(3) }
        val restored = StandaloneTable.gameFromJson(StandaloneTable.gameToJson(g))
        assertEquals(g.elder, restored.elder)
        assertEquals(g.elder, g.toSummary(10, 0).elder)
        assertNull(StandaloneGame().addPlayer("A").addPlayer("B").toSummary(0, 0).elder)
        val json = StandaloneTable.gameToJson(g)
        json.getJSONObject("elder").put("space", 12)
        assertEquals(ElderOneTrack(elderOne = "Hastur", episode = "Tomes of Madness"), StandaloneTable.gameFromJson(json).elder)
        val older = StandaloneTable.gameToJson(g).apply { remove("elder") }
        assertEquals(ElderOneTrack(), StandaloneTable.gameFromJson(older).elder)
    }

    @Test fun `the Elder One and episode lists name each once`() {
        val elders = DeathMayDieStory.elderOnes.flatMap { it.names }
        assertEquals(elders.size, elders.distinct().size)
        assertTrue("Cthulhu" in elders && "Hastur" in elders)
        val episodes = DeathMayDieStory.episodes.flatMap { it.names }
        assertEquals(episodes.size, episodes.distinct().size)
        assertTrue((elders + episodes).all { it.length <= ElderOneTrack.MAX_NAME })
    }

    @Test fun `fire tokens are counted, capped, saved and cleared by a rematch`() {
        val g = game()
        val burning = g.changeCounter(0, StandaloneGame.FIRE_KIND, 3)
        assertEquals(3, burning.players[0].dashboard.fire)
        assertEquals(InvestigatorDashboard.MAX_FIRE, burning.changeCounter(0, StandaloneGame.FIRE_KIND, 99).players[0].dashboard.fire)
        assertEquals(0, burning.changeCounter(0, StandaloneGame.FIRE_KIND, -3).players[0].dashboard.fire)
        assertSame(g, g.changeCounter(0, StandaloneGame.FIRE_KIND, -1))
        assertEquals(3, StandaloneTable.gameFromJson(StandaloneTable.gameToJson(burning)).players[0].dashboard.fire)
        assertEquals(0, burning.claimWin(0, 20).rematch(30, "e", Random(0)).players[0].dashboard.fire)
    }

    @Test fun `episode tokens are named once, counted in play and kept by name for a rematch`() {
        val named = lobby().nameEpisodeToken(" Gate ").nameEpisodeToken("gate").nameEpisodeToken("Seal")
        assertEquals(listOf("Gate", "Seal"), named.elder.tokens.map { it.name })
        // Counting waits for the game.
        assertSame(named, named.changeEpisodeToken(0, 1))
        val g = ready(named).chooseStarter(0).start(0, "d", Random(0)).changeEpisodeToken(0, 1).changeEpisodeToken(0, 1)
        assertEquals(2, g.elder.tokens[0].count)
        assertSame(g, g.changeEpisodeToken(1, -1))
        assertSame(g, g.changeEpisodeToken(7, 1))
        assertEquals(g.elder, StandaloneTable.gameFromJson(StandaloneTable.gameToJson(g)).elder)
        assertEquals(listOf(EpisodeToken("Gate"), EpisodeToken("Seal")), g.claimWin(0, 20).rematch(30, "e", Random(0)).elder.tokens)
        assertEquals(listOf("Seal"), g.removeEpisodeToken(0).elder.tokens.map { it.name })
        val full = (1..10).fold(ElderOneTrack()) { t, i -> t.withToken("T$i") }
        assertEquals(ElderOneTrack.MAX_TOKENS, full.tokens.size)
    }

    @Test fun `history keeps the Elder One, episode and investigators of a finished game`() {
        val g = ready(lobby()).setElderOne("Hastur").setEpisode("Tomes of Madness").chooseStarter(0).start(0, "d", Random(0))
        val lost = g.eliminate(1, 10)
        val record = lost.record()!!
        assertEquals("Hastur", record.elderOne)
        assertEquals("Tomes of Madness", record.episode)
        assertEquals("Investigator 0", record.players[0].investigator)
        assertEquals("Elder One: Hastur · Episode: Tomes of Madness", record.storyLine)
        val restored = GameRecord.fromJson(record.toJson())!!
        assertEquals(record, restored)
        val won = ready(lobby()).setElderOne("Hastur").chooseStarter(0).start(0, "f", Random(0)).claimWin(0, 10).record()!!
        val untracked = ready(lobby()).chooseStarter(0).start(0, "g", Random(0)).claimWin(0, 10).record()!!
        assertEquals(listOf(ElderOneResult("Hastur", 1, 1), ElderOneResult(UNRECORDED_ELDER_ONE, 1, 0)),
            LocalLibrary(history = listOf(record, won, untracked)).elderOneRecord())
        assertNull(untracked.storyLine)
    }
}
