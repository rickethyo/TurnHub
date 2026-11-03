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
 * protocol/info-v1.schema.json).
 *
 * This is a faithful wire DTO, not a domain model: every field mirrors the
 * schema 1:1, including the two identity consts ([product], [deviceType]).
 * [AtlasWireParser] fails closed if either const doesn't match, rather than
 * silently accepting a non-TurnHub responder. Whether the advertised API and
 * protocol versions are ones this app can speak is decided above this layer.
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
    /** Unsigned 32-bit Atlas revision, widened to [Long]. */
    val revision: Long,
    val capabilities: AtlasCapabilities,
    /** Where Atlas answers on the home network over HTTPS, or null (`home`). */
    val home: AtlasHomeAddress? = null,
)

/**
 * `/api/v1/info` `home`: Atlas's address on the home network, its HTTPS port
 * and [tlsPin], the lower-case hex SHA-256 of its key's SubjectPublicKeyInfo.
 * The app trusts the pin only when read over Atlas's own hotspot
 * (Documentation/engineering/HOME_WIFI.md).
 */
data class AtlasHomeAddress(
    val address: String,
    val tlsPort: Int,
    val tlsPin: String,
)
