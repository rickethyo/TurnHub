package com.turnhub.android.data

import com.turnhub.android.protocol.AtlasInfo
import com.turnhub.android.protocol.StateSnapshot

/**
 * The wire operations [HttpAtlasRepository] needs from one Atlas endpoint.
 * Implementations throw [AtlasException] for every failure they can classify
 * (network, HTTP status, not-TurnHub, malformed body).
 *
 * This is deliberately a narrow seam so repository behavior can be tested
 * against a fake without a network. Authenticated session calls
 * (profiles, login, join, `/api/session/me`, PASS) will be added here in the
 * next milestone; `/api/v1/intent` and events do not exist yet.
 */
interface AtlasTransport {
    /** `GET /api/v1/info`. */
    suspend fun getInfo(): AtlasInfo

    /** `GET /api/v1/state`. */
    suspend fun getState(): StateSnapshot
}

/** Builds a transport for the endpoint the user chose. */
fun interface AtlasTransportFactory {
    fun create(endpoint: AtlasEndpoint): AtlasTransport
}
