package com.turnhub.android.standalone

import com.turnhub.android.domain.LocalTimerRules
import com.turnhub.android.domain.TimeoutAction
import com.turnhub.android.domain.TableClock
import com.turnhub.android.protocol.TableState
import org.junit.Assert.*
import org.junit.Test

class LocalTimerRulesTest {
    private fun game(action: TimeoutAction = TimeoutAction.OVERTIME, extensions: Boolean = false,
        threshold: Int = 75, teams: Boolean = false): StandaloneGame = StandaloneGame(
        players = List(if (teams) 4 else 2) { LocalPlayer("Player $it") }, starter = 0,
        twoHeadedGiant = teams, turnTimerMs = 60_000,
        timerRules = LocalTimerRules(action, extensions, threshold),
    ).start(1_000, "test")

    @Test fun `timeout choices apply exactly once at the boundary`() {
        TimeoutAction.entries.forEach { action ->
            val initial = game(action)
            assertSame(initial, initial.applyTurnTimeout(60_999))
            val expired = initial.applyTurnTimeout(61_000)
            assertEquals(if (action.autoPass) 1 else 0, expired.active)
            assertEquals(if (action.demerit) 1 else 0, expired.players[0].demerits)
            assertSame(expired, expired.applyTurnTimeout(61_000))
            assertEquals(if (action.autoPass) 60_000L else 0L, expired.turnTimer(61_000).remainingMs)
        }
    }

    @Test fun `late tick settles one turn and gives next player a full clock`() {
        val next = game(TimeoutAction.PASS_AND_DEMERIT).applyTurnTimeout(9_000_000)
        assertEquals(1, next.active)
        assertEquals(listOf(1, 0), next.players.map { it.demerits })
        assertEquals(60_000L, next.turnTimer(9_000_000).remainingMs)
        assertSame(next, next.applyTurnTimeout(9_000_001))
    }

    @Test fun `pause freezes timeout and extension clocks`() {
        val paused = game(TimeoutAction.PASS).togglePause(31_000)
        assertSame(paused, paused.applyTurnTimeout(900_000))
        assertEquals(30_000L, paused.turnTimer(900_000).remainingMs)
        val resumed = paused.togglePause(900_000)
        assertEquals(0, resumed.applyTurnTimeout(929_999).active)
        assertEquals(1, resumed.applyTurnTimeout(930_000).active)
    }

    @Test fun `manual pass racing timeout never passes twice or earns a late reward`() {
        val initial = game(TimeoutAction.PASS_AND_DEMERIT, extensions = true)
        val result = initial.pass(0, 61_000)
        assertEquals(1, result.active)
        assertEquals(listOf(1, 0), result.players.map { it.turnsCompleted })
        assertEquals(1, result.players[0].demerits)
        assertEquals(0, result.players[0].extensionsEarned)
        assertSame(result, result.pass(0, 61_000))
    }

    @Test fun `first three personal turns earn nothing then fourth earns`() {
        var current = game(extensions = true)
        var now = 1_000L
        repeat(3) {
            now += 1_000; current = current.pass(0, now)
            now += 1_000; current = current.pass(1, now)
        }
        assertEquals(listOf(0, 0), current.players.map { it.extensionsEarned })
        current = current.pass(0, now + 1_000)
        assertEquals(1, current.players[0].extensionsEarned)
    }

    @Test fun `reward thresholds are strict at both 75 and 100 percent`() {
        listOf(75, 100).forEach { threshold ->
            val initial = game(extensions = true, threshold = threshold).let { g ->
                g.copy(players = g.players.map { it.copy(turnsCompleted = 3) })
            }
            val boundary = 1_000L + 60_000 * threshold / 100
            assertEquals(1, initial.pass(0, boundary - 1).players[0].extensionsEarned)
            assertEquals(0, initial.pass(0, boundary).players[0].extensionsEarned)
        }
    }

    @Test fun `extension spends one token adds half limit and cannot earn or stack`() {
        val initial = game(extensions = true).let { g -> g.copy(players = g.players.map {
            it.copy(turnsCompleted = 4, extensionsEarned = 2)
        }) }
        assertSame(initial, initial.extendTurn(1, 21_000))
        val extended = initial.extendTurn(0, 21_000)
        assertEquals(70_000L, extended.turnTimer(21_000).remainingMs)
        assertEquals(1, extended.players[0].extensionsUsed)
        assertSame(extended, extended.extendTurn(0, 22_000))
        assertEquals(2, extended.pass(0, 22_000).players[0].extensionsEarned)
        assertEquals(0L, extended.pass(0, 22_000).turnExtensionMs)
        val summary = extended.toSummary(92_000, 1_000)
        assertEquals(1_000L, TableClock.overtimeMs(summary, 1_000))
        assertSame(initial, initial.extendTurn(0, 61_000))
    }

    @Test fun `spent tokens do not reopen lifetime earning cap`() {
        val initial = game(extensions = true).let { g -> g.copy(players = g.players.map {
            it.copy(turnsCompleted = 8, extensionsEarned = 2, extensionsUsed = 2)
        }) }
        assertEquals(2, initial.pass(0, 2_000).players[0].extensionsEarned)
        assertSame(initial, initial.extendTurn(0, 2_000))
    }

    @Test fun `team shares rewards spending demerits and passes once`() {
        val initial = game(TimeoutAction.PASS_AND_DEMERIT, true, teams = true).let { g ->
            g.copy(players = g.players.map { it.copy(turnsCompleted = 3, extensionsEarned = 1) })
        }
        val extended = initial.extendTurn(1, 2_000)
        assertEquals(listOf(1, 1, 0, 0), extended.players.map { it.extensionsUsed })
        val next = extended.applyTurnTimeout(91_000)
        assertEquals(2, next.active)
        assertEquals(listOf(1, 1, 0, 0), next.players.map { it.demerits })
        assertEquals(0L, next.turnExtensionMs)
    }

    @Test fun `saved game restores timeout flags banks and extended clock`() {
        val extended = game(TimeoutAction.DEMERIT, true).let { g ->
            g.copy(players = g.players.map { it.copy(extensionsEarned = 1) })
        }.extendTurn(0, 2_000).applyTurnTimeout(91_000)
        val restored = StandaloneTable.gameFromJson(StandaloneTable.gameToJson(extended))
        assertEquals(extended, restored)
        assertSame(restored, restored.applyTurnTimeout(92_000))
        val reset = restored.reset()
        assertEquals(extended.timerRules, reset.timerRules)
        assertEquals(listOf(0, 0), reset.players.map { it.demerits + it.extensionsEarned + it.extensionsUsed })
        assertFalse(reset.timeoutHandled)
        assertEquals(0L, reset.turnExtensionMs)
        val record = restored.endWithoutWinner(92_000).record()!!
        assertEquals(record, GameRecord.fromJson(record.toJson()))
        assertEquals(1, record.players[0].demerits)
        assertEquals(1, record.players[0].extensionsUsed)
    }

    @Test fun `legacy saves default to overtime with no extension rules`() {
        val json = StandaloneTable.gameToJson(game())
        listOf("timerRules", "turnExtensionMs", "timeoutHandled").forEach(json::remove)
        val restored = StandaloneTable.gameFromJson(json)
        assertEquals(LocalTimerRules(), restored.timerRules)
        assertEquals(TableState.RUNNING, restored.applyTurnTimeout(90_000).state)
        assertEquals(0, restored.applyTurnTimeout(90_000).active)
    }
}
