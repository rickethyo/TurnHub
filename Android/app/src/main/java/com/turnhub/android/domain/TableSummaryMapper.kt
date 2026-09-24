package com.turnhub.android.domain

import com.turnhub.android.protocol.AtlasInfo
import com.turnhub.android.protocol.StateSnapshot

/**
 * Builds a [TableSummary] from one `/api/v1/info` response and one
 * `/api/v1/state` snapshot of the same Atlas boot. Pure translation: no game
 * rules, no values invented beyond neutral labels for unnamed players.
 */
object TableSummaryMapper {

    fun map(info: AtlasInfo, snapshot: StateSnapshot): TableSummary {
        require(info.atlasId == snapshot.atlasId && info.bootId == snapshot.bootId) {
            "Info and state must come from the same Atlas boot"
        }
        val players = snapshot.players.map { player ->
            TablePlayer(
                playerNumber = player.playerNumber,
                label = player.displayName?.takeIf { it.isNotBlank() } ?: "Player ${player.playerNumber}",
                controller = ControllerHandle(player.moduleId),
                slot = player.slot,
                participantId = player.participantId,
                eliminated = player.eliminated,
                life = player.life,
                turnsCompleted = player.turnsCompleted,
                commanderDamage = player.commanderDamage,
                lifeRequest = player.lifeRequest,
            )
        }
        return TableSummary(
            atlasId = snapshot.atlasId,
            bootId = snapshot.bootId,
            firmwareVersion = info.firmwareVersion,
            revision = snapshot.revision,
            state = snapshot.state,
            settings = snapshot.settings,
            host = snapshot.hostModuleId?.let(::ControllerHandle),
            starterPlayerNumber = snapshot.starterPlayer,
            activePlayerNumber = snapshot.activePlayer,
            winnerPlayerNumber = snapshot.winnerPlayer,
            pending = snapshot.pending,
            gameElapsedMs = snapshot.gameElapsedMs,
            turnElapsedMs = snapshot.turnElapsedMs,
            players = players,
            physicalSigils = players
                .filter { it.controller.kind == ControllerHandle.Kind.PHYSICAL }
                .groupBy { it.controller }
                .map { (controller, seated) ->
                    PhysicalSigilAtTable(
                        controller = controller,
                        seats = seated.sortedBy { it.slot }.map {
                            PhysicalSigilAtTable.Seat(it.slot, it.playerNumber, it.label)
                        },
                    )
                }
                .sortedBy { it.controller.id },
        )
    }
}
