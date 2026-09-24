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
    val pending: PendingDecisions,
    val players: List<Player>,
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
