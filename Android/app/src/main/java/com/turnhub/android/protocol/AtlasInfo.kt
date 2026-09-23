package com.turnhub.android.protocol

/** Mirrors the capability flags in protocol/info-v1.schema.json. */
data class AtlasCapabilities(
    val stateSnapshot: Boolean,
    val sessionControls: Boolean,
    val intentEnvelope: Boolean,
    val events: Boolean,
    val requestDeduplication: Boolean,
)

/**
 * Mirrors the response of `GET /api/v1/info` (protocol/http-v1.md,
 * protocol/info-v1.schema.json). Not yet rendered by this milestone's UI; kept
 * as a faithful model so the next slice (real `AtlasRepository`) has a ready
 * target to parse the real response into, per http-v1.md's suggested next
 * Android slice ("info -> login/join -> snapshot -> PASS -> snapshot -> reconnect").
 *
 * This is a faithful wire DTO, not a domain model: every field mirrors the
 * schema 1:1, including the two identity consts ([product], [deviceType]) that
 * the schema always sets to fixed values. A parser should still fail closed if
 * either const doesn't match, rather than silently accepting a non-Atlas/non-
 * TurnHub responder.
 */
data class AtlasInfo(
    val product: String,
    val deviceType: String,
    val atlasId: String,
    val firmwareVersion: String,
    val apiVersion: String,
    val protocolVersion: String,
    val radioProtocolVersion: Int,
    val bootId: String,
    val revision: Long,
    val capabilities: AtlasCapabilities,
)
