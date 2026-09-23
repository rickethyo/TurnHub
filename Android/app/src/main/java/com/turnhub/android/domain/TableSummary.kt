package com.turnhub.android.domain

import com.turnhub.android.protocol.Player
import com.turnhub.android.protocol.TableSettings
import com.turnhub.android.protocol.TableState

/**
 * A UI-oriented summary of one authoritative state snapshot
 * (protocol/state-v0.1.schema.json), combined with the identity fields Atlas
 * reports from `GET /api/v1/info` (protocol/http-v1.md).
 *
 * This lives in `domain/`, not `protocol/`, because it isn't a wire-format
 * object: no single Atlas response looks like this. A real implementation
 * builds it by combining an `/api/v1/info` response and an `/api/v1/state`
 * response inside `AtlasRepository`; for this milestone, `MockAtlasRepository`
 * fabricates it directly so the UI has something realistic to render.
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
