package com.turnhub.android.data

/** A seat at the table: its controller handle and slot. */
data class TabletSeat(val moduleId: Int, val slot: Int)

/** Planechase actions through [TableControls.control]. */
const val PLANAR_PREFIX = "planar:"

/**
 * What the table screen sends for a seat:
 * [com.turnhub.android.standalone.StandaloneTable] applies it to this device's
 * game, and a Play together guest's [com.turnhub.android.lan.LanTableControls]
 * sends it to the hub, whose rules decide.
 */
interface TableControls {
    /** `pass`, `pause`, `concede`, `win`, `confirm`, `deny`, `rematch` or `reset` (Atlas also takes the lobby ones). */
    suspend fun control(seat: TabletSeat, action: String): Boolean
    /** Remove this seat, optionally crediting another player number (null = unknown). */
    suspend fun eliminate(seat: TabletSeat, eliminatedBy: Int?): Boolean

    suspend fun resolveElimination(seat: TabletSeat, victim: Int, source: Int?, token: com.turnhub.android.protocol.GameActionToken): Boolean

    suspend fun doom(seat: TabletSeat, action: com.turnhub.android.protocol.DoomAction,
        token: com.turnhub.android.protocol.GameActionToken, player: Int = 0, value: Int = 0): Boolean

    /** A phone-hosted cooperative turn ends only after its recipient is chosen. */
    suspend fun passTo(turn: com.turnhub.android.domain.CooperativeTurn, playerNumber: Int): Boolean {
        error("Pass to requires a phone-hosted cooperative game")
    }

    /** Changes the seat's own life by [delta]; true when it was applied. */
    suspend fun life(seat: TabletSeat, delta: Int): Boolean

    /** Commander damage the seat received from [source]'s [commander] (1 or 2). */
    suspend fun commander(seat: TabletSeat, source: Int, commander: Int, delta: Int)

    suspend fun counter(seat: TabletSeat, kind: String, value: Int, commander: Int = 1)
    /** Editable reference text for the phone-only investigator dashboard. */
    suspend fun investigatorSkills(seat: TabletSeat, skills: List<com.turnhub.android.domain.InvestigatorSkill>) {
        error("Investigator dashboards require a phone-hosted game")
    }
    suspend fun investigatorCard(seat: TabletSeat, slot: com.turnhub.android.domain.InvestigatorCardSlot,
        card: com.turnhub.android.domain.InvestigatorCard?) {
        error("Investigator cards require a phone-hosted game")
    }
    suspend fun setEliminationRule(seat: TabletSeat, field: String, value: Int)

    suspend fun setAutoEliminateAtZeroLife(seat: TabletSeat, enabled: Boolean)

    /** Answers a phone's request to change this seat's life. */
    suspend fun respondLife(seat: TabletSeat, requestId: Long, accept: Boolean)

    /** Stops acting for the table. */
    suspend fun disable()
}
