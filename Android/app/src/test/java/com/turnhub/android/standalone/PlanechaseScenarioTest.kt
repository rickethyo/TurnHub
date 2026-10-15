package com.turnhub.android.standalone

import com.turnhub.android.protocol.GameProfile
import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import java.io.File
import kotlin.random.Random

/**
 * Runs the shared Planechase scenarios in protocol/scenarios/planechase/ (the
 * format is in protocol/scenarios/README.md) against the standalone engine.
 */
class PlanechaseScenarioTest {
    private val dir: File by lazy {
        generateSequence(File(System.getProperty("user.dir")!!).absoluteFile) { it.parentFile }
            .map { File(it, "protocol/scenarios/planechase") }
            .firstOrNull { it.isDirectory }
            ?: error("protocol/scenarios/planechase not found above ${System.getProperty("user.dir")}")
    }

    @Test fun `every shared Planechase scenario passes`() {
        val files = dir.listFiles { f -> f.extension == "json" }.orEmpty().sortedBy { it.name }
        assertTrue("no scenarios found", files.isNotEmpty())
        files.forEach { run(it.name, JSONObject(it.readText())) }
    }

    private fun run(name: String, scenario: JSONObject) {
        val setup = scenario.getJSONObject("setup")
        val planar = setup.getJSONObject("planechase")
        val phenomena = planar.getJSONArray("phenomena").strings().toSet()
        val deck = planar.getJSONArray("deck").strings().map { PlanarCard(it, it in phenomena) }
        var game = StandaloneGame()
            .setFormat(GameProfile.fromWire(setup.getString("profile"))!!)
        repeat(setup.getInt("players")) { game = game.addPlayer("Player ${it + 1}") }
        game = game.setTwoHeadedGiant(setup.optBoolean("twoHeadedGiant"))
            .chooseStarter(setup.getInt("starter") - 1)
            .setPlanechase(true)
            .setPlanarSource(PlanarDeckSource.fromWire(planar.getString("source"))!!)
        var now = 1_000L
        val steps = scenario.getJSONArray("steps")
        for (i in 0 until steps.length()) {
            val step = steps.getJSONObject(i)
            val at = "$name step ${i + 1} (${step.getString("action")})"
            val player = step.optInt("player") - 1
            now += 1_000
            val before = game
            game = when (step.getString("action")) {
                "start" -> game.start(now, "scenario", Random(0), deck)
                "roll" -> game.rollPlanarDie(player, PlanarFace.fromWire(step.getString("face"))!!, step.optBoolean("fromEffect"))
                "pass" -> game.pass(player, now)
                "planeswalk" -> game.planeswalk(player)
                "resolvePhenomenon" -> game.resolvePhenomenon(player)
                "chaosEnsues" -> game.chaosEnsues(player)
                "topToBottom" -> game.planarTopToBottom(player)
                "counter" -> game.changePlaneCounter(player, step.getString("card"), step.getInt("delta"))
                "eliminate" -> game.eliminate(player, now)
                "pause" -> game.togglePause(now)
                else -> error("$at: unknown action")
            }
            val expect = step.optJSONObject("expect") ?: JSONObject()
            assertEquals("$at: changed", expect.optBoolean("changed", true), game !== before)
            check(at, expect, game)
            // What is saved restores exactly.
            assertEquals("$at: saved game", game, StandaloneTable.gameFromJson(StandaloneTable.gameToJson(game)))
        }
    }

    private fun check(at: String, expect: JSONObject, game: StandaloneGame) {
        val planar = game.planechase
        if (expect.has("active")) assertEquals("$at: active", expect.getInt("active"), game.active?.plus(1))
        if (expect.has("faceUp")) assertEquals("$at: face up", expect.getJSONArray("faceUp").strings(), planar.faceUp.map { it.id })
        if (expect.has("deck")) assertEquals("$at: deck", expect.getJSONArray("deck").strings(), planar.deck.map { it.id })
        if (expect.has("phenomenonPending")) assertEquals("$at: phenomenon pending", expect.getBoolean("phenomenonPending"), planar.phenomenonPending)
        if (expect.has("planeswalks")) assertEquals("$at: planeswalks", expect.getInt("planeswalks"), planar.planeswalks)
        if (expect.has("chaos")) assertEquals("$at: chaos", expect.getInt("chaos"), planar.chaos)
        expect.optJSONObject("nextCost")?.let { costs ->
            costs.keys().forEach { assertEquals("$at: cost for player $it", costs.getInt(it), planar.cost(it.toInt() - 1)) }
        }
        expect.optJSONObject("counters")?.let { counters ->
            assertEquals("$at: counters", counters.keys().asSequence().associateWith { counters.getInt(it) }, planar.counters)
        }
        expect.optJSONObject("lastRoll")?.let { roll ->
            val last = planar.lastRoll
            assertEquals("$at: roll player", roll.getInt("player"), last?.player?.plus(1))
            assertEquals("$at: roll face", roll.getString("face"), last?.face?.wireValue)
            assertEquals("$at: roll cost", roll.getInt("cost"), last?.cost)
        }
    }

    private fun JSONArray.strings(): List<String> = (0 until length()).map { getString(it) }
}
