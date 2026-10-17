package com.turnhub.android.standalone

import com.turnhub.android.domain.DeathMayDie
import com.turnhub.android.domain.InvestigatorDashboard
import com.turnhub.android.domain.InvestigatorSkill
import com.turnhub.android.protocol.GameProfile
import com.turnhub.android.protocol.TableState
import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test
import kotlin.random.Random

class InvestigatorDashboardTest {
    private fun lobby() = StandaloneGame().setFormat(GameProfile.DEATH_MAY_DIE)
        .addPlayer("One").addPlayer("Two").setInvestigator(0, "Borden").setInvestigator(1, "Elizabeth Ives")
        .setInvestigatorCard(0, com.turnhub.android.domain.InvestigatorCardSlot.SANITY_CONDITION, com.turnhub.android.domain.InvestigatorCard("Condition A"))
        .setInvestigatorCard(1, com.turnhub.android.domain.InvestigatorCardSlot.SANITY_CONDITION, com.turnhub.android.domain.InvestigatorCard("Condition B"))
    private fun game() = lobby().chooseStarter(0).start(0, "d", Random(0))

    @Test fun `core investigators have complete distinct skills and four effects per skill`() {
        DeathMayDie.boxes.first().investigators.forEach { name ->
            val skills = InvestigatorDashboard().skills(name)
            assertEquals(name, 3, skills.map { it.name }.distinct().size)
            assertTrue(name, skills.all { it.levels.size == 4 && it.levels.none(String::isBlank) })
        }
        assertEquals(listOf("Savage", "Swiftness", "Stealth"), game().players[0].dashboard.skills("Borden").map { it.name })
    }

    @Test fun `sanity loss stops at each threshold and bonus dice follow the printed track`() {
        var board = InvestigatorDashboard()
        val positions = mutableListOf<Int>()
        val dice = mutableListOf<Int>()
        repeat(7) {
            board = board.changeInsanity(Int.MAX_VALUE)
            positions += board.insanity
            dice += board.bonusDice
        }
        assertEquals(listOf(4, 8, 12, 15, 18, 19, 20), positions)
        assertEquals(listOf(0, 1, 1, 2, 3, 4, 4), dice)
        assertSame(board, board.changeInsanity(1))
        assertEquals(0, board.changeInsanity(Int.MIN_VALUE).insanity)
    }

    @Test fun `skills and insanity are independent player markers and do not apply effects or eliminate`() {
        val g = game()
        val changed = g.changeCounter(0, StandaloneGame.SKILL_KIND, 4, 2)
            .changeCounter(0, StandaloneGame.INSANITY_KIND, 3)
        assertEquals(listOf(1, 4, 1), changed.players[0].dashboard.skillLevels)
        assertEquals(3, changed.players[0].dashboard.insanity)
        assertEquals(g.players[1].dashboard, changed.players[1].dashboard)
        assertEquals(g.players[0].life, changed.players[0].life)
        assertEquals(g.players[0].stress, changed.players[0].stress)
        val insane = (1..20).fold(changed) { state, _ -> state.changeCounter(0, StandaloneGame.INSANITY_KIND, 1) }
        assertEquals(TableState.RUNNING, insane.state)
        assertFalse(insane.players[0].eliminated)
    }

    @Test fun `marker edits reject invalid levels slots lifecycle and other game profiles`() {
        val g = game()
        listOf(0, 5, Int.MAX_VALUE).forEach { assertSame(g, g.changeCounter(0, StandaloneGame.SKILL_KIND, it, 1)) }
        listOf(0, 4, Int.MIN_VALUE).forEach { assertSame(g, g.changeCounter(0, StandaloneGame.SKILL_KIND, 2, it)) }
        assertSame(g, g.changeCounter(8, StandaloneGame.INSANITY_KIND, 1))
        val out = g.eliminate(0, 10)
        assertSame(out, out.changeCounter(0, StandaloneGame.INSANITY_KIND, 1))
        val ended = g.endWithoutWinner(10)
        assertSame(ended, ended.changeCounter(0, StandaloneGame.SKILL_KIND, 2, 1))
        val paused = g.togglePause(10)
        assertEquals(2, paused.changeCounter(0, StandaloneGame.SKILL_KIND, 2, 1).players[0].dashboard.skillLevels[0])
        val magic = StandaloneGame().addPlayer("A").addPlayer("B").start(0, "m", Random(0))
        assertSame(magic, magic.changeCounter(0, StandaloneGame.INSANITY_KIND, 1))
    }

    @Test fun `custom skills and progress survive saving while rematch returns fresh markers to setup`() {
        val skills = List(3) { i -> InvestigatorSkill("Custom $i", List(4) { j -> "Effect $i $j" }) }
        val customized = game().setInvestigatorSkills(0, skills).changeCounter(0, StandaloneGame.SKILL_KIND, 3, 1)
            .changeCounter(0, StandaloneGame.INSANITY_KIND, 4)
        val restored = StandaloneTable.gameFromJson(StandaloneTable.gameToJson(customized))
        assertEquals(customized.players.map { it.dashboard }, restored.players.map { it.dashboard })
        assertEquals(customized.players[0].dashboard, restored.toSummary(0, 0).players[0].dashboard)
        val rematch = restored.claimWin(restored.active!!, 20).rematch(30, "e", Random(0))
        assertEquals(TableState.LOBBY, rematch.state)
        assertFalse(rematch.canStart)
        assertEquals(0, rematch.players[0].dashboard.insanity)
        assertEquals(listOf(1, 1, 1), rematch.players[0].dashboard.skillLevels)
        assertEquals(skills, rematch.players[0].dashboard.skills("Borden"))
        assertEquals(skills, restored.reset().players[0].dashboard.customSkills)
        assertEquals(InvestigatorDashboard(), restored.setInvestigator(0, "Rasputin").players[0].dashboard)
    }

    @Test fun `custom reference validation preserves the existing dashboard on invalid input`() {
        val g = game()
        val skills = g.players[0].dashboard.skills("Borden")
        assertSame(g, g.setInvestigatorSkills(0, skills.take(2)))
        assertSame(g, g.setInvestigatorSkills(0, skills.map { it.copy(levels = listOf("missing")) }))
        assertSame(g, g.setInvestigatorSkills(0, skills.map { it.copy(name = " ") }))
        assertSame(g, g.setInvestigatorSkills(0, skills.map { it.copy(levels = List(4) { "x".repeat(601) }) }))
    }

    @Test fun `missing and malformed saved dashboard fields restore safe bounded markers`() {
        val json = StandaloneTable.gameToJson(game())
        val player = json.getJSONArray("players").getJSONObject(0)
        player.remove("dashboard")
        assertEquals(InvestigatorDashboard(), StandaloneTable.gameFromJson(json).players[0].dashboard)
        player.put("dashboard", JSONObject().put("insanity", 100).put("skillLevels", JSONArray(listOf(-2, 8)))
            .put("skills", JSONArray(listOf(JSONObject().put("name", "invalid")))))
        val board = StandaloneTable.gameFromJson(json).players[0].dashboard
        assertEquals(20, board.insanity)
        assertEquals(listOf(1, 4, 1), board.skillLevels)
        assertTrue(board.customSkills.isEmpty())
    }

    @Test fun `the last living investigator can finish turns and keep advancing rounds`() {
        val g = game().eliminate(1, 100)
        val next = g.pass(0, 1_000)
        assertEquals(TableState.RUNNING, next.state)
        assertEquals(2, next.round)
        assertEquals(0, next.active)
        assertEquals(1, next.players[0].turnsCompleted)
        assertEquals(3, next.pass(0, 2_000).round)
    }

    @Test fun `an expired last investigator turn passes once even when the active player stays the same`() {
        val g = game().eliminate(1, 100).copy(turnTimerMs = 1_000,
            timerRules = com.turnhub.android.domain.LocalTimerRules(timeout = com.turnhub.android.domain.TimeoutAction.PASS))
        val passed = g.pass(0, 2_000)
        assertEquals(2, passed.round)
        assertEquals(1, passed.players[0].turnsCompleted)
        assertEquals(0, passed.active)
    }
}
