package com.turnhub.android.standalone

import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith
import kotlin.random.Random

/** Executes the packaged NDK library on Android, including host-driven randomness. */
@RunWith(AndroidJUnit4::class)
class NativeCoreDeviceTest {
    @Test fun checkupUsesCallerCoinsAndEliminationResetsPlanarRollCosts() {
        val coins = object : Random() {
            override fun nextBits(bitCount: Int) = -1
            override fun nextBoolean() = true
        }
        val pokemon = StandaloneGame().setFormat(com.turnhub.android.protocol.GameProfile.POKEMON)
            .addPlayer("A").addPlayer("B").chooseStarter(0).start(0, "checkup")
            .changeCounter(1, "pokemon-condition:ASLEEP", 1)
            .changeCounter(1, "pokemon-condition:BURNED", 1).pass(0, 1000, coins)
        assertTrue(pokemon.players[1].turn.conditions.isEmpty())
        assertTrue(pokemon.pokemonCheckup.all { it.contains("heads") })
        val planar = StandaloneGame().addPlayer("A").addPlayer("B").addPlayer("C").setPlanechase(true)
            .chooseStarter(0).start(0, "cost").rollPlanarDie(0, PlanarFace.BLANK)
        assertEquals(1, planar.planechase.cost(0))
        val after = planar.eliminate(0, 1000)
        assertTrue(after.planechase.rolls.isEmpty())
        assertEquals(1, after.active)
    }

    @Test fun sharedScenariosRunThroughThePackagedCore() {
        val assets = InstrumentationRegistry.getInstrumentation().context.assets
        val scenarios = assets.list("")!!.filter { it.endsWith(".json") }.sorted()
        assertTrue(scenarios.isNotEmpty())
        for (file in scenarios) {
            var game = StandaloneGame()
            val steps = JSONObject(assets.open(file).bufferedReader().use { it.readText() }).getJSONArray("steps")
            for (index in 0 until steps.length()) {
                val step = steps.getJSONObject(index)
                val action = JSONObject(step.getJSONObject("action").toString())
                if (action.getString("do") == "seat") {
                    action.put("match", action.optString("match").ifEmpty { game.gameId })
                    action.put("revision", game.revision - if (action.optBoolean("stale")) 1 else 0)
                }
                val draws = step.optJSONArray("random") ?: JSONArray()
                var used = 0
                val random = object : Random() {
                    override fun nextBits(bitCount: Int): Int = error("Unexpected random bits")
                    override fun nextInt(until: Int): Int = draws.getInt(used++).also { require(it in 0 until until) }
                    override fun nextBoolean() = nextInt(2) != 0
                }
                val before = game
                game = NativeGameCore.apply(game, action, random)
                val where = "$file step ${index + 1}"
                assertEquals("$where random draws", draws.length(), used)
                if (step.optBoolean("unchanged")) assertSame(where, before, game)
                val state = StandaloneTable.gameToJson(game).put("profile", game.profile.name)
                    .put("winClaim", game.winClaim ?: JSONObject.NULL).put("winConfirmed", JSONArray(game.winConfirmed.sorted()))
                step.optJSONObject("expect")?.let { expected ->
                    expected.keys().forEach { path -> assertTrue("$where $path: ${at(state, path)} != ${expected.get(path)}",
                        same(expected.get(path), at(state, path))) }
                }
                if (step.has("record")) assertTrue("$where record", same(step.get("record"),
                    NativeGameCore.record(game)?.toJson() ?: JSONObject.NULL))
            }
        }
    }

    @Test fun unicodeNamesSurviveTheJniBoundary() {
        val game = StandaloneGame().addPlayer("Zoë ⚙️ 🐉").addPlayer("友達").chooseStarter(0).start(1000, "unicode")
        assertEquals("Zoë ⚙️ 🐉", game.players.first().name)
        assertEquals("友達", game.players.last().name)
    }

    private fun at(root: Any, path: String): Any = path.split('.').fold(root) { node, part ->
        when (node) { is JSONObject -> node.get(part); is JSONArray -> node.get(part.toInt()); else -> error(path) }
    }
    private fun same(a: Any, b: Any): Boolean = when {
        a is Number && b is Number -> a.toDouble() == b.toDouble()
        a is JSONObject && b is JSONObject -> a.keys().asSequence().toSet().let { keys ->
            keys == b.keys().asSequence().toSet() && keys.all { same(a.get(it), b.get(it)) }
        }
        a is JSONArray && b is JSONArray -> a.length() == b.length() && (0 until a.length()).all { same(a.get(it), b.get(it)) }
        else -> a == b
    }
}
