package com.turnhub.android.standalone

import com.turnhub.android.domain.*
import com.turnhub.android.protocol.GameProfile
import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test
import kotlin.random.Random

class DeathMayDieHelpersTest {
    private val condition = InvestigatorCardSlot.SANITY_CONDITION
    private val relic = InvestigatorCardSlot.RELIC
    private fun lobby() = StandaloneGame().setFormat(GameProfile.DEATH_MAY_DIE)
        .addPlayer("A").addPlayer("B").setInvestigator(0, "Rasputin").setInvestigator(1, "Borden")
    private fun game() = lobby().setInvestigatorCard(0, condition, InvestigatorCard("A condition"))
        .setInvestigatorCard(1, condition, InvestigatorCard("B condition")).chooseStarter(0).start(0, "g", Random(0))

    @Test fun `house rule deals stable independent pairs without replacing reserved cards`() {
        val setup = lobby().setUseRelics(true).setDrawTwoCards(true)
        val dealt = setup.dealInvestigatorCards(0, condition, Random(0)).dealInvestigatorCards(1, condition, Random(1))
            .dealInvestigatorCards(0, relic, Random(2)).dealInvestigatorCards(1, relic, Random(3))
        val ids = dealt.players.flatMap { it.dashboard.cardOffers.values.flatten() }
        assertEquals(8, ids.size)
        assertEquals(8, ids.toSet().size)
        dealt.players.forEach { p -> p.dashboard.cardOffers.forEach { (slot, pair) ->
            assertEquals(2, pair.size)
            assertTrue(pair.all { InvestigatorCardCatalogue.find(it)?.slot == slot })
        } }
        assertSame(dealt, dealt.dealInvestigatorCards(0, condition, Random(987)))
        val pair = dealt.players[0].dashboard.cardOffers.getValue(condition)
        val chosen = dealt.setInvestigatorCard(0, condition, InvestigatorCardCatalogue.find(pair[1])!!.card())
        assertEquals(pair, chosen.players[0].dashboard.cardOffers[condition])
        assertEquals(pair[1], chosen.players[0].dashboard.sanityCondition!!.catalogueId)
        assertEquals(dealt.players.map { it.dashboard.cardOffers }, chosen.reset().players.map { it.dashboard.cardOffers })
        val restored = StandaloneTable.gameFromJson(StandaloneTable.gameToJson(chosen))
        assertTrue(restored.drawTwoCards)
        assertEquals(chosen.players.map { it.dashboard }, restored.players.map { it.dashboard })
        assertTrue(restored.toSummary(0, 0).drawTwoCards)
        assertEquals(pair, restored.players[0].dashboard.cardOffers[condition])
        assertSame(restored, restored.dealInvestigatorCards(0, condition))
        val disabled = restored.setUseRelics(false)
        assertTrue(disabled.players.all { it.dashboard.cardOffers[relic] == null })
    }

    @Test fun `dealing and toggles validate lifecycle while physical draws remain valid`() {
        val off = lobby()
        assertSame(off, off.dealInvestigatorCards(0, condition))
        val setup = off.setDrawTwoCards(true)
        assertSame(setup, setup.dealInvestigatorCards(0, relic))
        assertSame(setup, setup.dealInvestigatorCards(-1, condition))
        val physical = setup.setInvestigatorCard(0, condition, InvestigatorCard("Physical choice A"))
            .setInvestigatorCard(1, condition, InvestigatorCard("Physical choice B"))
        assertTrue(physical.canStart)
        val running = physical.chooseStarter(0).start(0, "p", Random(0))
        assertSame(running, running.setDrawTwoCards(false))
        assertSame(running, running.dealInvestigatorCards(0, condition))
        val rematch = running.claimWin(0, 10).rematch(20, "r", Random(0))
        assertTrue(rematch.drawTwoCards)
        assertTrue(rematch.players.all { it.dashboard.sanityCondition == null && it.dashboard.cardOffers.isEmpty() })
        val magic = StandaloneGame().addPlayer("A").addPlayer("B")
        assertSame(magic, magic.setDrawTwoCards(true))
        assertSame(magic, magic.dealInvestigatorCards(0, condition))
    }

    @Test fun `fifth ability survives recovery and keeps its marker when fourth is removed`() {
        val original = game()
        val skills = original.players[0].dashboard.skills("Rasputin")
        val swift = InvestigatorSkills.presets.first { it.name == "Swiftness" }
        val added = original.setInvestigatorSkills(0, skills + swift + InvestigatorSkill("Episode gift"))
            .changeCounter(0, StandaloneGame.SKILL_KIND, 3, 4).changeCounter(0, StandaloneGame.SKILL_KIND, 4, 5)
        assertEquals(listOf(1, 1, 1, 3, 4), added.players[0].dashboard.skillLevels)
        val restored = StandaloneTable.gameFromJson(StandaloneTable.gameToJson(added))
        assertEquals(added.players[0].dashboard, restored.players[0].dashboard)
        val removed = restored.setInvestigatorSkills(0, skills + InvestigatorSkill("Episode gift"))
        assertEquals(listOf(1, 1, 1, 4), removed.players[0].dashboard.skillLevels)
        assertEquals(added.players[1].dashboard, removed.players[1].dashboard)
        assertEquals(skills, restored.reset().players[0].dashboard.skills("Rasputin"))
        assertEquals(listOf(1, 1, 1), restored.reset().players[0].dashboard.skillLevels)
        val renamed = restored.setInvestigatorSkills(0, skills + swift + InvestigatorSkill("Renamed gift"))
        assertEquals(listOf(1, 1, 1, 3, 4), renamed.players[0].dashboard.skillLevels)
        val ended = restored.claimWin(0, 10)
        assertSame(ended, ended.setInvestigatorSkills(0, skills))
        assertSame(original, original.setInvestigatorSkills(0, skills + skills[0]))
        assertSame(original, original.setInvestigatorSkills(0, List(9) { InvestigatorSkill("Skill $it") }))
    }

    @Test fun `legacy and malformed pairs restore safely without discarding assigned physical cards`() {
        val json = StandaloneTable.gameToJson(lobby().setDrawTwoCards(true))
        json.remove("drawTwoCards")
        val d = json.getJSONArray("players").getJSONObject(0).getJSONObject("dashboard")
        val id = InvestigatorCardCatalogue.all.first { it.slot == condition }.id
        d.put("cardOffers", JSONObject().put(condition.name, JSONArray(listOf(id, id)))
            .put(relic.name, JSONArray(listOf(id, "unknown"))))
        val restored = StandaloneTable.gameFromJson(json)
        assertFalse(restored.drawTwoCards)
        assertTrue(restored.players[0].dashboard.cardOffers.isEmpty())
        d.put("skills", JSONArray(List(4) { JSONObject().put("name", "Duplicate") }))
        assertEquals(3, StandaloneTable.gameFromJson(json).players[0].dashboard.skillLevels.size)
    }

    @Test fun `random investigator respects search skill and taken figures including case`() {
        repeat(30) { seed ->
            val name = InvestigatorIndex.random("", "Brawling", setOf("RASPUTIN"), Random(seed))!!
            assertNotEquals("Rasputin", name)
            assertTrue(InvestigatorIndex.matches(name, "", "Brawling"))
            assertTrue(name in DeathMayDie.all)
        }
        assertNull(InvestigatorIndex.random("Rasputin", "Brawling", setOf("rasputin"), Random(0)))
        assertEquals("Rasputin", InvestigatorIndex.random("rasputin", "Brawling", emptySet(), Random(0)))
        assertTrue(InvestigatorIndex.matches("Sergeant Ian Welles", "brawling", "Swiftness"))
        assertFalse(InvestigatorIndex.matches("Unknown custom investigator", "", "Brawling"))
        assertNull(InvestigatorIndex.random("No such investigator", null, emptySet()))
        assertTrue(InvestigatorIndex.skills("Adilah Vidal")!!.contains("Brawling"))
        assertEquals(3, InvestigatorSkills.forInvestigator("Adilah Vidal").size)
        assertFalse(InvestigatorSkills.hasReference("Adilah Vidal"))
    }
}
