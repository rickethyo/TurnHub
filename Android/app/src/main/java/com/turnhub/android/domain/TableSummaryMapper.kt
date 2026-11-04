package com.turnhub.android.domain

import com.turnhub.android.protocol.AtlasInfo
import com.turnhub.android.protocol.GameProfile
import com.turnhub.android.protocol.PendingDecisions
import com.turnhub.android.protocol.StateSnapshot
import com.turnhub.android.protocol.TableSettings
import com.turnhub.android.protocol.TableState
import com.turnhub.android.protocol.TurnTimer
import com.turnhub.android.protocol.TurnTimerPhase

/**
 * Builds the connected Atlas's [TableSummary] from one `/api/v1/info` response
 * and one `/api/v1/state` snapshot of the same boot. Atlas runs no game (the
 * phone hub does, protocol/hosted-v1.md), so the summary carries Atlas's
 * identity, battery, links and the hub holding its table, never players.
 */
object TableSummaryMapper {

    fun map(info: AtlasInfo, snapshot: StateSnapshot, receivedAtMs: Long = 0): TableSummary {
        require(info.atlasId == snapshot.atlasId && info.bootId == snapshot.bootId) {
            "Info and state must come from the same Atlas boot"
        }
        return TableSummary(
            atlasId = snapshot.atlasId,
            bootId = snapshot.bootId,
            firmwareVersion = info.firmwareVersion,
            revision = snapshot.revision,
            state = TableState.LOBBY,
            settings = TableSettings(GameProfile.GENERIC, startingLife = 0),
            host = null,
            starterPlayerNumber = null,
            activePlayerNumber = null,
            winnerPlayerNumber = null,
            pending = PendingDecisions(null, 0, null, null, null),
            gameElapsedMs = 0,
            turnElapsedMs = 0,
            turnTimer = TurnTimer(TurnTimerPhase.NORMAL, null),
            receivedAtMs = receivedAtMs,
            players = emptyList(),
            physicalSigils = emptyList(),
            battery = snapshot.battery,
            links = snapshot.links,
            hub = snapshot.hub,
        )
    }
}
