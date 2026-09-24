package com.turnhub.android.domain

import com.turnhub.android.protocol.CommanderDamage
import com.turnhub.android.protocol.LifeRequest
import com.turnhub.android.protocol.PendingDecisions
import com.turnhub.android.protocol.TableSettings
import com.turnhub.android.protocol.TableState
import com.turnhub.android.protocol.TurnTimer

/**
 * A UI-oriented summary of one authoritative state snapshot
 * (protocol/state-v0.1.schema.json), combined with the identity fields Atlas
 * reports from `GET /api/v1/info` (protocol/http-v1.md).
 *
 * This lives in `domain/`, not `protocol/`, because no single Atlas response
 * looks like this. It is always rebuilt whole from the latest snapshot
 * ([TableSummaryMapper]); nothing in it is advanced locally, so it is a cache
 * of Atlas state for rendering, never a second source of truth.
 */
data class TableSummary(
    val atlasId: String,
    val bootId: String,
    val firmwareVersion: String,
    /** Unsigned 32-bit revision of the snapshot this summary was built from. */
    val revision: Long,
    val state: TableState,
    val settings: TableSettings,
    val host: ControllerHandle?,
    val starterPlayerNumber: Int?,
    val activePlayerNumber: Int?,
    val winnerPlayerNumber: Int?,
    val pending: PendingDecisions,
    /** Atlas-sampled clocks; may change while [revision] stays the same. */
    val gameElapsedMs: Long,
    val turnElapsedMs: Long,
    /** Atlas's turn-timer phase and countdown sample; null from older firmware. */
    val turnTimer: TurnTimer?,
    /**
     * Local monotonic time (ms) when this snapshot arrived. Lets the UI render
     * clocks between polls as `sampled + (now - receivedAtMs)`; the next
     * snapshot always replaces that estimate (protocol/README.md allows
     * locally rendered time-sensitive display between snapshots).
     */
    val receivedAtMs: Long,
    val players: List<TablePlayer>,
    /** Physical Sigils currently represented at the table, derived from [players]. */
    val physicalSigils: List<PhysicalSigilAtTable>,
) {
    /**
     * A finished match with no winner: the table ended it as a draw by holding
     * the Atlas master button (protocol/http-v1.md). Derived, never stored.
     */
    val endedInDraw: Boolean
        get() = state == TableState.GAME_OVER && winnerPlayerNumber == null
}

/** A controller seat: handle + slot. How `/api/seats` names are matched to players. */
data class SeatKey(val moduleId: Int, val slot: Int)

/** One participant as the Home screen renders it. */
data class TablePlayer(
    val playerNumber: Int,
    /** Atlas-provided name (state, else `/api/seats`), or a neutral "Player N". */
    val label: String,
    /** True when [label] came from Atlas rather than the "Player N" fallback. */
    val hasName: Boolean,
    val controller: ControllerHandle,
    val slot: Int,
    val participantId: Long,
    val eliminated: Boolean,
    /** Null in the lobby. */
    val life: Int?,
    val turnsCompleted: Long,
    val commanderDamage: List<CommanderDamage>,
    val lifeRequest: LifeRequest?,
)

/**
 * A physical Sigil that is seated at the current table, and which seats it
 * holds. Built only from `players[]`: it says nothing about pairing,
 * connectivity or Sigils that are paired but not seated.
 */
data class PhysicalSigilAtTable(
    val controller: ControllerHandle,
    val seats: List<Seat>,
) {
    data class Seat(val slot: Int, val playerNumber: Int, val label: String)
}

/**
 * Atlas's controller handle (`moduleId` on the wire). Physical Sigil handles are
 * 0-7 and virtual/browser handles 8-23 (protocol/http-v1.md). A handle is not a
 * durable device ID.
 */
@JvmInline
value class ControllerHandle(val id: Int) {
    val kind: Kind
        get() = when (id) {
            in PHYSICAL -> Kind.PHYSICAL
            in VIRTUAL -> Kind.VIRTUAL
            else -> Kind.UNKNOWN
        }

    enum class Kind { PHYSICAL, VIRTUAL, UNKNOWN }

    companion object {
        val PHYSICAL = 0..7
        val VIRTUAL = 8..23
    }
}

/** Seat letter used by Atlas and the portal: slot 1 = A, slot 2 = B. */
fun seatLabel(slot: Int): String = when (slot) {
    1 -> "A"
    2 -> "B"
    else -> "slot $slot"
}
