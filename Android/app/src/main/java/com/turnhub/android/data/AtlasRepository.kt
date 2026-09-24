package com.turnhub.android.data

import com.turnhub.android.domain.TableSummary
import com.turnhub.android.protocol.AtlasConnectionState
import com.turnhub.android.protocol.Sigil
import kotlinx.coroutines.flow.StateFlow

/**
 * The seam between UI/ViewModel code and however the app actually talks to an
 * Atlas. [MockAtlasRepository] is the only implementation for this milestone; a
 * future networking implementation (HTTP against protocol/http-v1.md, per
 * Android/README.md's "First vertical slice") swaps in behind this same
 * interface without the UI layer changing.
 *
 * This boundary is deliberate, not incidental: Android/README.md's hard
 * architecture rule is that this app renders Atlas state and requests actions,
 * it never becomes a second source of truth. Keeping all Atlas access behind
 * one interface is what makes that rule checkable later -- nothing above this
 * interface should need to know whether the connection is real or mocked.
 */
interface AtlasRepository {
    val connectionState: StateFlow<AtlasConnectionState>
    val tableSummary: StateFlow<TableSummary?>
    val sigils: StateFlow<List<Sigil>>

    /** Requests a connection to an Atlas (mocked for this milestone). */
    suspend fun connect()

    /** Requests disconnection from the current Atlas. */
    suspend fun disconnect()
}
