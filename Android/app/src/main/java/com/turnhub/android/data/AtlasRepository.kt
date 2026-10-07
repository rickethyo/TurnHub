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
 * [tableSummary] is therefore only ever a copy of the latest Atlas snapshot.
 * When Atlas stops answering it stays on screen as the last known state, marked
 * by [offlineSinceMs], and is cleared when the connection is ended.
 */
interface AtlasRepository {
    val connectionState: StateFlow<AtlasConnectionState>

    /** The endpoint of the current or most recent connection attempt. */
    val endpoint: StateFlow<AtlasEndpoint?>

    /** Latest authoritative table state; non-null only while [AtlasConnectionState.CONNECTED]. */
    val tableSummary: StateFlow<TableSummary?>

    /**
     * When Atlas stopped answering (local [com.turnhub.android.domain.TableClock]
     * ms), or null while it answers. While set, the connection stays CONNECTED,
     * [tableSummary] is the last state Atlas sent, and polling carries on until
     * Atlas answers again or [disconnect] is called.
     */
    val offlineSinceMs: StateFlow<Long?> get() = NEVER_OFFLINE

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

    /**
     * While [hold] is true, failed polls stay transient (reported in [failure])
     * instead of dropping the connection: Atlas is busy serving a Sigil its
     * update and answers again shortly. Dropping would also give up Atlas's
     * Wi-Fi and the signed-in session the update needs.
     */
    fun holdThroughOutages(hold: Boolean) {}
}

private val NEVER_OFFLINE: StateFlow<Long?> = kotlinx.coroutines.flow.MutableStateFlow(null)
