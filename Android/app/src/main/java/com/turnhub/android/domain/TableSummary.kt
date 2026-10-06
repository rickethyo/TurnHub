package com.turnhub.android.domain

import com.turnhub.android.protocol.AvatarIcon
import com.turnhub.android.protocol.CommanderDamage
import com.turnhub.android.protocol.LifeRequest
import com.turnhub.android.protocol.Nudge
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
    /** Atlas's turn-timer phase and countdown sample. */
    val turnTimer: TurnTimer,
    /**
     * Local monotonic time (ms) when this snapshot arrived. Lets the UI render
     * clocks between polls as `sampled + (now - receivedAtMs)`; the next
     * snapshot always replaces that estimate (protocol/README.md allows
     * locally rendered time-sensitive display between snapshots).
     */
    val receivedAtMs: Long,
    /** Atlas's uptime clock when it sampled this snapshot (unsigned 32-bit, wraps). */
    val sampledAtMs: Long = 0,
    val players: List<TablePlayer>,
    /** Physical Sigils currently represented at the table, derived from [players]. */
    val physicalSigils: List<PhysicalSigilAtTable>,
    /** The last nudge since Atlas booted; its age is as of [sampledAtMs]. */
    val nudge: Nudge? = null,
) {
    /**
     * A finished match with no winner: the table ended it as a draw by holding
     * End match on the Atlas touchscreen (protocol/http-v1.md). Derived, never stored.
     */
    val endedInDraw: Boolean
        get() = state == TableState.GAME_OVER && winnerPlayerNumber == null

    private fun teamOf(playerNumber: Int?): Int? = players.firstOrNull { it.playerNumber == playerNumber }?.team

    /** The same player, or Two-Headed Giant teammates (Atlas reports each player's team). */
    fun sameTeam(a: Int?, b: Int?): Boolean =
        a != null && b != null && (a == b || (teamOf(a) != null && teamOf(a) == teamOf(b)))

    /** The active player, or (Two-Headed Giant) their teammate: the turn is the team's. */
    fun hasTurn(playerNumber: Int): Boolean = sameTeam(activePlayerNumber, playerNumber)

    /** "Team N" for a Two-Headed Giant player, else null (name the player). */
    fun teamLabel(playerNumber: Int?): String? = teamOf(playerNumber)?.let { "Team $it" }

    /** The winner, or (Two-Headed Giant) the winner's teammate. */
    fun isWinner(playerNumber: Int): Boolean = sameTeam(winnerPlayerNumber, playerNumber)
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
    /** Two-Headed Giant team (players 1+2 are team 1); null outside Two-Headed Giant. */
    val team: Int? = null,
    val participantId: Long,
    val eliminated: Boolean,
    /** Null in the lobby. */
    val life: Int?,
    val turnsCompleted: Long,
    val commanderDamage: List<CommanderDamage>,
    val lifeRequest: LifeRequest?,
    /** The seat's preset avatar, when Atlas has one for it. */
    val avatar: AvatarIcon? = null,
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
