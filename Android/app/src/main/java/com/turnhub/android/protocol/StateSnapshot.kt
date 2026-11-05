package com.turnhub.android.protocol

/**
 * Mirrors the response of `GET /api/v1/state` (protocol/atlas-state-v1.schema.json):
 * what an Atlas accessory says about itself. The phone hub runs every game
 * (protocol/hosted-v1.md); Atlas reports only which phone holds the table, its
 * battery and its links.
 *
 * Identity is the triple ([atlasId], [bootId], [revision]); [revision] moves when
 * the [hub] summary changes. [battery] and [links] are sampled and may change
 * while it stays the same.
 */
data class StateSnapshot(
    val protocolVersion: String,
    val atlasId: String,
    val bootId: String,
    /** Unsigned 32-bit. */
    val revision: Long,
    /** The phone that holds the table's lease, or null. */
    val hub: AtlasHub? = null,
    /** Atlas's own battery; null with no cell. Sampled, not versioned. */
    val battery: AtlasBattery? = null,
    /** Atlas's links, as its header icons show them. Sampled, not versioned. */
    val links: AtlasLinks? = null,
)

/** The phone hub holding an Atlas's table: its name and the game it shows there. */
data class AtlasHub(
    val name: String,
    val gameId: String,
    val state: TableState,
    val players: Int,
)

/**
 * Mirrors `battery` in protocol/atlas-state-v1.schema.json: Atlas's cell, estimated
 * from its voltage. [low] is at or below 15 %; while [charging] (on USB) the
 * percent counts up.
 */
data class AtlasBattery(
    val percent: Int,
    val low: Boolean,
    val charging: Boolean,
)

/**
 * Mirrors `links` in protocol/atlas-state-v1.schema.json: Atlas's own hotspot is
 * up, it is on the home network, and how many Sigils are online.
 */
data class AtlasLinks(
    val hotspot: Boolean,
    val home: Boolean,
    val sigils: Int,
)

/**
 * A player prodding the active player in a hub's game. [seq] grows with every
 * nudge, so a client alerts [toPlayer] once per new [seq].
 */
data class Nudge(
    /** Unsigned 32-bit. */
    val seq: Long,
    val fromPlayer: Int,
    val toPlayer: Int,
    /** Age when the game's state was sampled. */
    val ageMs: Long,
)

/** Decisions the game is waiting on; null player fields mean "none". */
data class PendingDecisions(
    val passPlayer: Int?,
    val passGraceRemainingMs: Long,
    val winClaimPlayer: Int?,
    val winConfirmationPlayer: Int?,
    val eliminationTargetPlayer: Int?,
)
