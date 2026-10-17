package com.turnhub.android.standalone

import com.turnhub.android.domain.InvestigatorCard
import com.turnhub.android.domain.InvestigatorCardSlot
import com.turnhub.android.domain.InvestigatorDashboard
import com.turnhub.android.domain.InvestigatorSkill
import com.turnhub.android.protocol.GameProfile
import com.turnhub.android.protocol.TableState
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test
import kotlin.random.Random

class InvestigatorCardTest {
    private val condition = InvestigatorCard("Test condition", "Resolve the printed symptoms.")
    private val relic = InvestigatorCard("Test relic", "A player-entered relic reference.")
    private val sanitySlot = InvestigatorCardSlot.SANITY_CONDITION
    private val relicSlot = InvestigatorCardSlot.RELIC
    private fun lobby() = StandaloneGame().setFormat(GameProfile.DEATH_MAY_DIE).addPlayer("A").addPlayer("B")
        .setInvestigator(0, "Borden").setInvestigator(1, "Elizabeth Ives").chooseStarter(0)
    private fun assigned() = lobby().setInvestigatorCard(0, sanitySlot, condition).setInvestigatorCard(0, relicSlot, relic)
    private fun game() = assigned().start(0, "d", Random(0))

    @Test fun `each investigator owns two independent replaceable removable slots`() {
        val reseated = assigned().swapPlayers(0, 1)
        assertEquals("Borden", reseated.players[1].investigator)
        assertEquals(condition, reseated.players[1].dashboard.sanityCondition)
        val g = game().changeLife(0, 2).changeCounter(0, StandaloneGame.STRESS_KIND, 1)
            .changeCounter(0, StandaloneGame.INSANITY_KIND, 4).changeCounter(0, StandaloneGame.SKILL_KIND, 2, 1)
        assertEquals(condition, g.players[0].dashboard.card(sanitySlot))
        assertEquals(relic, g.players[0].dashboard.card(relicSlot))
        assertEquals(InvestigatorDashboard(), g.players[1].dashboard)
        val changed = g.setInvestigatorCard(0, sanitySlot, InvestigatorCard(" New condition ", " Notes\n\nMore notes "))
        assertEquals(InvestigatorCard("New condition", "Notes\n\nMore notes"), changed.players[0].dashboard.sanityCondition)
        assertEquals(relic, changed.players[0].dashboard.relic)
        assertEquals(g.players[0].dashboard.insanity, changed.players[0].dashboard.insanity)
        assertEquals(g.players[0].dashboard.skillLevels, changed.players[0].dashboard.skillLevels)
        assertEquals(g.players[0].life, changed.players[0].life)
        assertEquals(g.players[0].stress, changed.players[0].stress)
        val removed = changed.setInvestigatorCard(0, relicSlot, null)
        assertNull(removed.players[0].dashboard.relic)
        assertEquals(changed.players[0].dashboard.sanityCondition, removed.players[0].dashboard.sanityCondition)
        assertSame(removed, removed.setInvestigatorCard(0, relicSlot, null))
        assertNull(removed.setInvestigatorCard(0, sanitySlot, null).players[0].dashboard.sanityCondition)
    }

    @Test fun `card edits validate input and leave the previous assignment intact on failure`() {
        val g = game()
        listOf(InvestigatorCard(" "), InvestigatorCard("x".repeat(81)), InvestigatorCard("Name", "x".repeat(2_001))).forEach {
            assertSame(g, g.setInvestigatorCard(0, sanitySlot, it))
        }
        assertSame(g, g.setInvestigatorCard(-1, relicSlot, relic))
        assertSame(g, g.setInvestigatorCard(8, relicSlot, relic))
        assertSame(g, g.setInvestigatorCard(0, sanitySlot, condition))
        val maximum = InvestigatorCard("x".repeat(80), "r".repeat(2_000))
        assertEquals(maximum, g.setInvestigatorCard(0, relicSlot, maximum).players[0].dashboard.relic)
        assertEquals(InvestigatorCard("Name only"), g.setInvestigatorCard(0, relicSlot, InvestigatorCard("Name only")).players[0].dashboard.relic)
    }

    @Test fun `setup running and paused games allow assignments while stopped or fallen players do not`() {
        assertEquals(condition, assigned().players[0].dashboard.sanityCondition)
        val g = game()
        val other = InvestigatorCard("Another relic")
        assertEquals(other, g.setInvestigatorCard(0, relicSlot, other).players[0].dashboard.relic)
        val paused = g.togglePause(50)
        assertEquals(other, paused.setInvestigatorCard(0, relicSlot, other).players[0].dashboard.relic)
        listOf(g.eliminate(0, 50), g.claimWin(0, 50), g.copy(state = TableState.STARTING),
            g.copy(profile = GameProfile.MTG_COMMANDER)).forEach {
            assertSame(it, it.setInvestigatorCard(0, relicSlot, other))
            assertSame(it, it.setInvestigatorCard(0, relicSlot, null))
        }
    }

    @Test fun `both cards save and restore with literal notes and appear in the table summary`() {
        val special = InvestigatorCard("Example \"card\" Ω", "Line one\nLine two <literal notes>")
        val g = game().setInvestigatorCard(1, relicSlot, special)
        val restored = StandaloneTable.gameFromJson(StandaloneTable.gameToJson(g))
        assertEquals(g.players.map { it.dashboard }, restored.players.map { it.dashboard })
        assertEquals(condition, restored.toSummary(0, 0).players[0].dashboard.sanityCondition)
        assertEquals(special, restored.toSummary(0, 0).players[1].dashboard.relic)
        val cleared = restored.setInvestigatorCard(0, sanitySlot, null)
        assertNull(StandaloneTable.gameFromJson(StandaloneTable.gameToJson(cleared)).players[0].dashboard.sanityCondition)
        val setup = assigned()
        assertEquals(setup.players.map { it.dashboard }, StandaloneTable.gameFromJson(StandaloneTable.gameToJson(setup)).players.map { it.dashboard })
    }

    @Test fun `missing null malformed or oversized card data does not corrupt the other slot or markers`() {
        val g = game().changeCounter(0, StandaloneGame.INSANITY_KIND, 4)
        val json = StandaloneTable.gameToJson(g)
        val dashboard = json.getJSONArray("players").getJSONObject(0).getJSONObject("dashboard")
        val bad = listOf(JSONObject.NULL, "not an object", JSONObject().put("name", 12),
            JSONObject().put("name", " "), JSONObject().put("name", "x".repeat(81)),
            JSONObject().put("name", "Good").put("rules", 12), JSONObject().put("name", "Good").put("rules", "x".repeat(2_001)))
        bad.forEach { value ->
            dashboard.put("sanityCondition", value)
            val board = StandaloneTable.gameFromJson(json).players[0].dashboard
            assertNull(board.sanityCondition)
            assertEquals(relic, board.relic)
            assertEquals(4, board.insanity)
        }
        dashboard.remove("sanityCondition")
        dashboard.remove("relic")
        val empty = StandaloneTable.gameFromJson(json).players[0].dashboard
        assertNull(empty.sanityCondition)
        assertNull(empty.relic)
        dashboard.put("relic", JSONObject().put("name", "Name only"))
        assertEquals(InvestigatorCard("Name only"), StandaloneTable.gameFromJson(json).players[0].dashboard.relic)
    }

    @Test fun `start and back to setup retain chosen cards but rematch clears them and keeps skill references`() {
        val custom = List(3) { InvestigatorSkill("Skill $it", List(4) { "Player-entered effect" }) }
        val g = game().setInvestigatorSkills(0, custom).changeCounter(0, StandaloneGame.INSANITY_KIND, 4)
        val setup = g.backToSetup(100)
        assertEquals(condition, setup.players[0].dashboard.sanityCondition)
        assertEquals(relic, setup.start(200, "e", Random(0)).players[0].dashboard.relic)
        val next = g.claimWin(0, 100).rematch(200, "new", Random(0))
        assertNull(next.players[0].dashboard.sanityCondition)
        assertNull(next.players[0].dashboard.relic)
        assertEquals("Borden", next.players[0].investigator)
        assertEquals(custom, next.players[0].dashboard.customSkills)
        assertEquals(0, next.players[0].dashboard.insanity)
        assertEquals(listOf(1, 1, 1), next.players[0].dashboard.skillLevels)
        assertEquals(InvestigatorDashboard(), g.setInvestigator(0, "Rasputin").players[0].dashboard)
    }
}
