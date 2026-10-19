package com.turnhub.android.data

/**
 * What the tablet table screen sends for a seat. [AtlasTablet] sends each to
 * Atlas, which decides it; [com.turnhub.android.standalone.StandaloneTable]
 * applies it to the app's own standalone game when no Atlas is at the table.
 */
interface TableControls {
    /** `pass`, `pause`, `concede`, `win`, `confirm`, `deny`, `rematch` or `reset` (Atlas also takes the lobby ones). */
    suspend fun control(seat: TabletSeat, action: String): Boolean
    /** Remove this seat, optionally crediting another player number (null = unknown). */
    suspend fun eliminate(seat: TabletSeat, eliminatedBy: Int?): Boolean

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

    /** Keeps a change for Atlas while it isn't answering. */
    fun queueOffline(change: OfflineChange)

    /** Refuses an action that waits for Atlas while it isn't answering. */
    fun refuseOffline()

    /** Stops acting for the table. */
    suspend fun disable()
}
