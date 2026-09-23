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
 */
data class AtlasInfo(
    val atlasId: String,
    val firmwareVersion: String,
    val apiVersion: String,
    val protocolVersion: String,
    val bootId: String,
    val revision: Long,
    val capabilities: AtlasCapabilities,
)
