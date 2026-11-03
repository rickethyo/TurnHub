package com.turnhub.android.lan

import com.turnhub.android.data.*
import com.turnhub.android.domain.TableSummary
import com.turnhub.android.protocol.*

/** Shared rendering sends the same seat actions through the room authority. */
internal class LanTableControls(
    private val summary: TableSummary,
    private val act: (String, Int, Int, Int, Int) -> Unit,
    private val refused: () -> Unit,
    private val disconnect: () -> Unit = refused,
) : TableControls {
    constructor(model: LanViewModel, summary: TableSummary) : this(summary,
        { kind, value, slot, source, target ->
            if (model.atTable(summary)) model.act(kind, value, slot, source, target) else model.unsupportedAction()
        }, model::unsupportedAction, model::leave)
    private fun send(seat: TabletSeat, kind: String, value: Int = 0, slot: Int = 0, source: Int = 0): Boolean {
        val player = summary.players.firstOrNull { it.controller.id == seat.moduleId && it.slot == seat.slot } ?: return false
        act(kind, value, slot, source, player.playerNumber)
        return true
    }
    override suspend fun life(seat: TabletSeat, delta: Int) = send(seat, if (summary.settings.profile.deathMayDie) "wounds" else "life", delta)
    override suspend fun commander(seat: TabletSeat, source: Int, commander: Int, delta: Int) { send(seat, "commander", delta, commander, source) }
    override suspend fun counter(seat: TabletSeat, kind: String, value: Int, commander: Int) {
        val skill = kind == com.turnhub.android.standalone.StandaloneGame.SKILL_KIND
        val slot = if (summary.settings.profile.deathMayDie && !skill || kind.startsWith(com.turnhub.android.standalone.StandaloneGame.ACTION_KIND) ||
            kind.startsWith(com.turnhub.android.standalone.StandaloneGame.CONDITION_KIND) || kind == com.turnhub.android.standalone.StandaloneGame.RAD_KIND) 0 else commander
        send(seat, if (skill) "skill" else kind, value, slot)
    }
    override suspend fun passTo(turn: com.turnhub.android.domain.CooperativeTurn, playerNumber: Int): Boolean {
        act("pass", playerNumber, 0, 0, turn.playerNumber); return true
    }
    override suspend fun investigatorSkills(seat: TabletSeat, skills: List<com.turnhub.android.domain.InvestigatorSkill>) { refused() }
    override suspend fun investigatorCard(seat: TabletSeat, slot: com.turnhub.android.domain.InvestigatorCardSlot, card: com.turnhub.android.domain.InvestigatorCard?) { refused() }
    override suspend fun control(seat: TabletSeat, action: String) = send(seat, action,
        if (action == "pause") if (summary.state == TableState.RUNNING) 1 else 0 else 0)
    override suspend fun eliminate(seat: TabletSeat, eliminatedBy: Int?) = send(seat, "eliminate")
    override suspend fun resolveElimination(seat: TabletSeat, victim: Int, source: Int?, token: GameActionToken): Boolean { refused(); return false }
    override suspend fun doom(seat: TabletSeat, action: DoomAction, token: GameActionToken, player: Int, value: Int): Boolean { refused(); return false }
    override suspend fun setEliminationRule(seat: TabletSeat, field: String, value: Int) { refused() }
    override suspend fun setAutoEliminateAtZeroLife(seat: TabletSeat, enabled: Boolean) { refused() }
    override suspend fun respondLife(seat: TabletSeat, requestId: Long, accept: Boolean) { refused() }
    override fun queueOffline(change: OfflineChange) { refused() }
    override fun refuseOffline() { refused() }
    override suspend fun disable() { disconnect() }
}
