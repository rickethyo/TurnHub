package com.turnhub.android.protocol

/**
 * A controller-facing summary of one paired Sigil.
 *
 * ASSUMPTION: Atlas's current HTTP surface (protocol/http-v1.md) does not expose
 * a dedicated "list of paired Sigils" endpoint yet -- only `players[]`, which mixes
 * physical Sigil channels (moduleId 0-7) with virtual/browser handles (moduleId
 * 8-23; see http-v1.md, "Revisions and reconnect"). This shape is a first cut
 * built from that addressing rule plus the device-identity concept sketched in
 * Documentation/engineering/PROTOCOL_AND_PAIRING.md ("device_id, hardware_revision,
 * firmware_version, ..., paired_atlas_id, friendly_name") and the persisted
 * MAC-to-slot pairing described in Documentation/engineering/MANUAL_PAIRING.md
 * (`th_pair_v1` slots s0-s7, capacity eight). It is intentionally not presented
 * as a frozen contract -- replace or correct it once Atlas exposes a real
 * pairing/device-roster endpoint.
 */
data class Sigil(
    val id: String,
    val name: String,
    val moduleId: Int,
    val connected: Boolean,
    val assignedPlayerNumber: Int?,
)
