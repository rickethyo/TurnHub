package com.turnhub.android.testing

import com.turnhub.android.domain.ControllerHandle
import com.turnhub.android.domain.TablePlayer
import com.turnhub.android.domain.TableSummary
import com.turnhub.android.protocol.GameProfile
import com.turnhub.android.protocol.PendingDecisions
import com.turnhub.android.protocol.TableSettings
import com.turnhub.android.protocol.TableState
import com.turnhub.android.protocol.TurnTimer
import com.turnhub.android.protocol.TurnTimerPhase

/**
 * A two-player game as a hub's [TableSummary]: player 1 on this phone (handle 8)
 * is active, 40 life each, no timer. Tests change it with `copy`.
 */
object TableFixtures {
    fun player(number: Int, handle: Int = 7 + number) = TablePlayer(
        playerNumber = number,
        label = "Player $number",
        hasName = false,
        controller = ControllerHandle(handle),
        slot = 1,
        participantId = 27L + number,
        eliminated = false,
        life = 40,
        turnsCompleted = 0,
        commanderDamage = emptyList(),
        lifeRequest = null,
    )

    fun running(receivedAtMs: Long = 50_000, state: TableState = TableState.RUNNING) = TableSummary(
        atlasId = "THA-025448410001",
        bootId = Fixtures.BOOT_ID,
        firmwareVersion = "0.6.0-dev",
        revision = 4,
        state = state,
        settings = TableSettings(GameProfile.GENERIC, startingLife = 40, turnTimerMs = 0),
        host = ControllerHandle(8),
        starterPlayerNumber = 1,
        activePlayerNumber = 1,
        winnerPlayerNumber = null,
        pending = PendingDecisions(null, 0, null, null, null),
        gameElapsedMs = 100,
        turnElapsedMs = 100,
        turnTimer = TurnTimer(TurnTimerPhase.NORMAL, null),
        receivedAtMs = receivedAtMs,
        players = listOf(player(1), player(2)),
        physicalSigils = emptyList(),
    )

    /** A 90-second turn timer in its warning phase: 9 s left after 81 s. */
    fun timerWarning(receivedAtMs: Long = 50_000) = running(receivedAtMs).let {
        it.copy(settings = it.settings.copy(profile = GameProfile.MTG, startingLife = 20, turnTimerMs = 90_000),
            turnElapsedMs = 81_000, turnTimer = TurnTimer(TurnTimerPhase.WARNING, 9_000))
    }
}
