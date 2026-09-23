package com.turnhub.android.protocol

/**
 * A UI-oriented summary of one authoritative state snapshot
 * (protocol/state-v0.1.schema.json), combined with the identity fields Atlas
 * reports from `GET /api/v1/info` (protocol/http-v1.md).
 *
 * A real implementation builds this by combining an `/api/v1/info` response and
 * an `/api/v1/state` response inside `AtlasRepository`; for this milestone,
 * `MockAtlasRepository` fabricates it directly so the UI has something realistic
 * to render.
 */
data class TableSummary(
    val atlasId: String,
    val firmwareVersion: String,
    val revision: Long,
    val state: TableState,
    val settings: TableSettings,
    val activePlayerNumber: Int?,
    val players: List<Player>,
)
