package com.turnhub.android.data

import com.turnhub.android.domain.TableSummary
import com.turnhub.android.protocol.AtlasConnectionState
import kotlinx.coroutines.flow.StateFlow

/**
 * The seam between UI/ViewModel code and however the app talks to an Atlas.
 * [HttpAtlasRepository] is the production implementation.
 *
 * Android/README.md's hard architecture rule is that this app renders Atlas
 * state and requests actions; it never becomes a second source of truth.
 * [tableSummary] is therefore only ever a copy of the latest Atlas snapshot,
 * and is cleared rather than left stale when the connection ends.
 */
interface AtlasRepository {
    val connectionState: StateFlow<AtlasConnectionState>

    /** The endpoint of the current or most recent connection attempt. */
    val endpoint: StateFlow<AtlasEndpoint?>

    /** Latest authoritative table state; non-null only while [AtlasConnectionState.CONNECTED]. */
    val tableSummary: StateFlow<TableSummary?>

    /**
     * Why the last attempt failed, or why the connection was dropped. While
     * still CONNECTED this is a transient polling failure being retried.
     */
    val failure: StateFlow<AtlasFailure?>

    /**
     * Connects to [endpoint]: validates `/api/v1/info`, loads `/api/v1/state`,
     * reports CONNECTED, then keeps polling state. Suspends until connected or
     * failed. Does nothing unless currently DISCONNECTED, so a second tap can
     * never start a second polling loop.
     */
    suspend fun connect(endpoint: AtlasEndpoint)

    /** Stops polling and clears live state. Safe to call in any state. */
    suspend fun disconnect()
}
