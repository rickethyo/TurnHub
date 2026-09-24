package com.turnhub.android.protocol

/**
 * One entry of `GET /api/seats` (`seats[]`): public presentation metadata for
 * an occupied controller seat. This is where Atlas publishes display names;
 * `/api/v1/state` does not carry them (protocol/http-v1.md).
 *
 * `/api/seats` predates the versioned v1 contract, so only the fields the app
 * uses are modeled and parsed.
 */
data class SeatEntry(
    /** Controller handle, same as `moduleId` in `/api/v1/state`. */
    val moduleId: Int,
    val slot: Int,
    val playerNumber: Int,
    /** Saved profile name, or null when the seat has none (e.g. a guest). */
    val name: String?,
)
