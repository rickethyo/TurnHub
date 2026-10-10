package com.turnhub.android.protocol

/**
 * Mirrors the response of `GET /api/v1/state` (protocol/state-v0.1.schema.json):
 * one authoritative Atlas snapshot.
 *
 * Identity is the triple ([atlasId], [bootId], [revision]). [revision] versions
 * the gameplay projection; the sampled clock fields ([sampledAtMs],
 * [gameElapsedMs], [turnElapsedMs], [PendingDecisions.passGraceRemainingMs])
 * may change while [revision] stays the same. All `*Ms` values are Atlas uptime
 * milliseconds that wrap at 32 bits and are only comparable within one boot.
 */
data class StateSnapshot(
    val protocolVersion: String,
    val atlasId: String,
    val bootId: String,
    /** Unsigned 32-bit. */
    val revision: Long,
    val state: TableState,
    /** Controller handle of the table host, if any. */
    val hostModuleId: Int?,
    val starterPlayer: Int?,
    val activePlayer: Int?,
    val winnerPlayer: Int?,
    val settings: TableSettings,
    val sampledAtMs: Long,
    val gameElapsedMs: Long,
    val turnElapsedMs: Long,
    val turnTimer: TurnTimer,
    val pending: PendingDecisions,
    val players: List<Player>,
    /** The last nudge since Atlas booted, or null. */
    val nudge: Nudge? = null,
    /** Atlas's own battery; null with no cell or from older firmware. Sampled, not versioned. */
    val battery: AtlasBattery? = null,
    val game: Int = 1,
    val games: List<VenueGame> = emptyList(),
)

/**
 * Mirrors `battery` in protocol/state-v0.1.schema.json: Atlas's cell, estimated
 * from its voltage. [low] is at or below 15 %; while [charging] (on USB) the
 * percent counts up.
 */
data class AtlasBattery(
    val percent: Int,
    val low: Boolean,
    val charging: Boolean,
)

/**
 * Mirrors `nudge` in protocol/state-v0.1.schema.json: a player prodding the
 * active player (POST /api/control/nudge). [seq] grows with every nudge since
 * boot, so a client alerts [toPlayer] once per new [seq].
 */
data class Nudge(
    /** Unsigned 32-bit. */
    val seq: Long,
    val fromPlayer: Int,
    val toPlayer: Int,
    /** Age when [StateSnapshot.sampledAtMs] was taken. */
    val ageMs: Long,
)

/**
 * Mirrors the `pending` object in protocol/state-v0.1.schema.json: decisions
 * Atlas is waiting on. Every field is optional in the schema; absent player
 * fields mean "none" and an absent grace time means zero.
 */
data class PendingDecisions(
    val passPlayer: Int?,
    val passGraceRemainingMs: Long,
    val winClaimPlayer: Int?,
    val winConfirmationPlayer: Int?,
    val eliminationTargetPlayer: Int?,
)

/** Read-only venue overview supplied by Atlas. Game numbers are 1-based. */
data class VenueGame(val game: Int, val state: TableState, val players: Int)
