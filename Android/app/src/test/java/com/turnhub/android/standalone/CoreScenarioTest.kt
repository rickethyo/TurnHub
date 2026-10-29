package com.turnhub.android.standalone

import com.turnhub.android.domain.DayNight
import com.turnhub.android.domain.LocalTimerRules
import com.turnhub.android.domain.TimeoutAction
import com.turnhub.android.protocol.GameProfile
import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.assertTrue
import org.junit.Test
import java.io.File
import kotlin.random.Random

/**
 * Replays the shared scenarios in the repository's `core/scenarios/` against
 * [StandaloneGame]. turnhub-core (the C++ port of these rules) replays the same
 * files in `core/tests/`, so the app and Atlas answer every scenario alike.
 * [coreJson] writes the game with the field names the core prints.
 */
class CoreScenarioTest {

    private val scenarios: File by lazy {
        generateSequence(File(System.getProperty("user.dir")!!).absoluteFile) { it.parentFile }
            .map { File(it, "core/scenarios") }
            .firstOrNull { it.isDirectory }
            ?: error("core/scenarios not found above ${System.getProperty("user.dir")}")
    }

    /** Hands out a step's "random" list in order. */
    private class ScriptedRandom(private val draws: List<Int>) : Random() {
        var used = 0
        var overrun = false
        override fun nextBits(bitCount: Int): Int = error("scenario random used through nextBits")
        override fun nextInt(until: Int): Int {
            if (used >= draws.size) { overrun = true; return 0 }
            return draws[used++].also { if (it !in 0 until until) overrun = true }
        }
        override fun nextInt(from: Int, until: Int): Int = from + nextInt(until - from)
    }

    @Test
    fun everyScenarioMatchesTheKotlinRules() {
        val files = scenarios.listFiles { f -> f.name.endsWith(".json") }!!.sortedBy { it.name }
        assertTrue("no scenarios in $scenarios", files.isNotEmpty())
        val failures = files.flatMap { replay(JSONObject(it.readText())) }
        assertTrue(failures.joinToString("\n"), failures.isEmpty())
    }

    private fun replay(scenario: JSONObject): List<String> {
        val name = scenario.getString("name")
        val failures = mutableListOf<String>()
        var game = StandaloneGame()
        val steps = scenario.getJSONArray("steps")
        for (s in 0 until steps.length()) {
            val step = steps.getJSONObject(s)
            val action = step.getJSONObject("action")
            val draws = step.optJSONArray("random")?.let { a -> (0 until a.length()).map { a.getInt(it) } } ?: emptyList()
            val random = ScriptedRandom(draws)
            val before = game
            game = apply(game, action, random)
            val state = coreJson(game)
            val where = "$name step ${s + 1} (${action.getString("do")})"
            if (random.overrun || random.used != draws.size) failures += "$where: used ${random.used} of ${draws.size} random draws"
            if (step.optBoolean("unchanged") && (game.revision != before.revision || !same(withoutRevision(state), withoutRevision(coreJson(before))))) {
                failures += "$where: expected no change"
            }
            step.optJSONObject("expect")?.let { expect ->
                expect.keys().forEach { path ->
                    val actual = at(state, path)
                    if (!same(actual, expect.get(path))) failures += "$where: $path = $actual, expected ${expect.get(path)}"
                }
            }
            step.optJSONObject("timer")?.let { timer ->
                val t = game.turnTimer(timer.getLong("nowMs"))
                if (timer.has("phase") && timer.getString("phase") != t.phase.name) failures += "$where: timer phase ${t.phase}, expected ${timer.get("phase")}"
                if (timer.has("remainingMs") && !same(t.remainingMs ?: JSONObject.NULL, timer.get("remainingMs"))) {
                    failures += "$where: timer remainingMs ${t.remainingMs}, expected ${timer.get("remainingMs")}"
                }
            }
        }
        return failures
    }

    /** The action vocabulary of core/README.md, mapped to the Kotlin rules. */
    private fun apply(g: StandaloneGame, a: JSONObject, random: Random): StandaloneGame {
        val now = a.optLong("nowMs")
        fun i(key: String) = a.getInt(key)
        fun optInt(key: String): Int? = if (a.isNull(key)) null else a.getInt(key)
        return when (a.getString("do")) {
            "addPlayer" -> g.addPlayer(a.getString("name"), a.optString("profileId").ifEmpty { null }, a.optString("localId").ifEmpty { null })
            "removePlayer" -> g.removePlayer(i("index"))
            "clearPlayers" -> g.clearPlayers()
            "movePlayer" -> g.movePlayer(i("index"), i("by"))
            "swapPlayers" -> g.swapPlayers(i("a"), i("b"))
            "chooseStarter" -> g.chooseStarter(optInt("index"))
            "setFormat" -> g.setFormat(GameProfile.valueOf(a.getString("profile")), optInt("life"))
            "setHouseRules" -> g.setHouseRules(a.getBoolean("on"))
            "setTwoHeadedGiant" -> g.setTwoHeadedGiant(a.getBoolean("on"))
            "setStartingLife" -> g.setStartingLife(i("life"))
            "setTurnTimer" -> g.setTurnTimer(a.getLong("ms"))
            "setTimerRules" -> runCatching {
                LocalTimerRules(TimeoutAction.valueOf(a.getString("timeout")), a.getBoolean("earnExtensions"),
                    a.getInt("earnBeforePercent"), a.getInt("maxEarnedPerGame"))
            }.getOrNull()?.let(g::setTimerRules) ?: g
            "start" -> g.start(now, a.getString("gameId"), random)
            "reset" -> g.reset()
            "backToSetup" -> g.backToSetup(now)
            "applyTurnTimeout" -> g.applyTurnTimeout(now)
            "extendTurn" -> g.extendTurn(i("index"), now)
            "pass" -> g.pass(i("index"), now, random)
            "togglePause" -> g.togglePause(now)
            "changeLife" -> g.changeLife(i("index"), i("delta"), now)
            "commanderDamage" -> g.commanderDamage(i("index"), i("source"), i("commander"), i("delta"), now)
            "setDayNight" -> g.setDayNight(DayNight.valueOf(a.getString("value")))
            "setTurnOrderReversed" -> g.setTurnOrderReversed(a.getBoolean("on"))
            "claimMonarch" -> g.claimMonarch(i("index"))
            "changeCounter" -> g.changeCounter(i("index"), a.getString("kind"), i("value"), a.optInt("commander", 1), now)
            "changeCounterForEveryone" -> g.changeCounterForEveryone(a.getString("kind"), i("delta"), now)
            "setEliminationRule" -> g.setEliminationRule(a.getString("field"), i("value"), now)
            "concede" -> g.concede(i("index"), now)
            "eliminate" -> g.eliminate(i("index"), now, a.optBoolean("conceded"), optInt("by"), a.optBoolean("self"))
            "resolveElimination" -> g.resolveElimination(i("victim"), optInt("source"), a.optBoolean("self"), now)
            "requestWin" -> g.requestWin(i("index"), now)
            "answerWinClaim" -> g.answerWinClaim(i("index"), a.getBoolean("accept"), now)
            "claimWin" -> g.claimWin(i("index"), now)
            "endWithoutWinner" -> g.endWithoutWinner(now)
            "rematch" -> g.rematch(now, a.getString("gameId"), random)
            "seanModeRematch" -> g.seanModeRematch(now, a.getString("gameId"), random)
            "doomEligibility" -> g.doomEligibility(i("player"), a.getBoolean("inBag"), a.getLong("cycle"))
            "doomCredit" -> g.doomCredit(i("victim"), i("source"), a.getLong("cycle"))
            "drawDoom" -> g.drawDoom(a.getLong("cycle"), now, random)
            else -> error("unknown action ${a.getString("do")}")
        }
    }

    private fun coreJson(g: StandaloneGame): JSONObject = JSONObject().apply {
        put("gameId", g.gameId)
        put("state", g.state.name)
        put("profile", g.profile.name)
        put("startingLife", g.startingLife)
        put("autoEliminateAtZeroLife", g.autoEliminateAtZeroLife)
        put("askEliminationSource", g.askEliminationSource)
        put("autoEliminateAtPoisonLimit", g.autoEliminateAtPoisonLimit)
        put("autoEliminateAtCommanderDamage", g.autoEliminateAtCommanderDamage)
        put("poisonLimit", g.poisonLimit)
        put("turnTimerMs", g.turnTimerMs)
        put("timerRules", JSONObject().put("timeout", g.timerRules.timeout.name).put("earnExtensions", g.timerRules.earnExtensions)
            .put("earnBeforePercent", g.timerRules.earnBeforePercent).put("maxEarnedPerGame", g.timerRules.maxEarnedPerGame))
        put("turnExtensionMs", g.turnExtensionMs)
        put("timeoutHandled", g.timeoutHandled)
        put("twoHeadedGiant", g.twoHeadedGiant)
        put("houseRules", g.houseRules)
        put("players", JSONArray(g.players.map { p ->
            JSONObject().apply {
                put("name", p.name)
                put("profileId", p.profileId ?: JSONObject.NULL)
                put("localId", p.localId ?: JSONObject.NULL)
                put("life", p.life)
                put("eliminated", p.eliminated)
                put("conceded", p.conceded)
                put("eliminatedBy", p.eliminatedBy ?: JSONObject.NULL)
                put("selfEliminated", p.selfEliminated)
                put("eliminationPending", p.eliminationPending)
                put("counters", JSONObject().put("poison", p.counters.poison).put("energy", p.counters.energy)
                    .put("experience", p.counters.experience).put("commanderCasts", JSONArray(p.counters.commanderCasts))
                    .put("cityBlessing", p.counters.cityBlessing).put("treasure", p.counters.treasure).put("rad", p.counters.rad))
                put("outOrder", p.outOrder ?: JSONObject.NULL)
                put("turnsCompleted", p.turnsCompleted)
                put("turnMs", p.turnMs)
                put("fastestTurnMs", p.fastestTurnMs)
                put("longestTurnMs", p.longestTurnMs)
                put("commanderDamage", JSONObject().apply { p.commanderDamage.toSortedMap().forEach { (k, v) -> put(k.toString(), JSONArray(v)) } })
                put("demerits", p.demerits)
                put("extensionsEarned", p.extensionsEarned)
                put("extensionsUsed", p.extensionsUsed)
                put("commander", p.commander ?: JSONObject.NULL)
                put("commanderArt", p.commanderArt ?: JSONObject.NULL)
                put("cardOnly", p.cardOnly)
            }
        }))
        put("active", g.active ?: JSONObject.NULL)
        put("starter", g.starter ?: JSONObject.NULL)
        put("winner", g.winner ?: JSONObject.NULL)
        put("winners", JSONArray(g.winners))
        put("monarch", g.monarch ?: JSONObject.NULL)
        put("initiative", g.initiative ?: JSONObject.NULL)
        put("dayNight", g.dayNight.name)
        put("turnOrderReversed", g.turnOrderReversed)
        put("winClaim", g.winClaim ?: JSONObject.NULL)
        put("winConfirmed", JSONArray(g.winConfirmed.sorted()))
        put("startedAtMs", g.startedAtMs)
        put("elapsedMs", g.elapsedMs)
        put("runningSinceMs", g.runningSinceMs ?: JSONObject.NULL)
        put("turnStartedAtElapsedMs", g.turnStartedAtElapsedMs)
        val b = g.doomBag
        put("doomBag", JSONObject().put("atRisk", b.atRisk).put("drawn", b.drawn).put("boundary", b.boundary).put("warmup", b.warmup)
            .put("cycle", b.cycle).put("pending", b.pending).put("pendingTurn", b.pendingTurn).put("selected", b.selected)
            .put("sealed", b.sealed).put("lastDraw", b.lastDraw).put("lastCheck", b.lastCheck))
        put("radDue", JSONArray(g.radDue))
        put("revision", g.revision)
    }

    private fun withoutRevision(state: JSONObject) = JSONObject(state.toString()).apply { remove("revision") }

    private fun at(root: JSONObject, path: String): Any {
        var node: Any = root
        for (part in path.split('.')) {
            node = when (node) {
                is JSONObject -> if (node.has(part)) node.get(part) else return "<missing>"
                is JSONArray -> part.toIntOrNull()?.takeIf { it in 0 until node.length() }?.let(node::get) ?: return "<missing>"
                else -> return "<missing>"
            }
        }
        return node
    }

    /** Deep equality that treats every number by value, as the C++ runner does. */
    private fun same(a: Any?, b: Any?): Boolean = when {
        a is Number && b is Number -> a.toDouble() == b.toDouble()
        a is JSONArray && b is JSONArray -> a.length() == b.length() && (0 until a.length()).all { same(a.get(it), b.get(it)) }
        a is JSONObject && b is JSONObject -> a.keys().asSequence().toSet().let { keys ->
            keys == b.keys().asSequence().toSet() && keys.all { same(a.get(it), b.get(it)) }
        }
        else -> a == b
    }
}
