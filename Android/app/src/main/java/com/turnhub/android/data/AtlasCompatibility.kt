package com.turnhub.android.data

import com.turnhub.android.protocol.AtlasInfo
import com.turnhub.android.protocol.StateSnapshot

/**
 * The contract versions this app speaks. A responder that already passed the
 * TurnHub/Atlas identity check in [com.turnhub.android.protocol.AtlasWireParser]
 * is still refused here if it advertises a different HTTP API or logical
 * protocol, or does not publish state snapshots (protocol/README.md: "Clients
 * must fail clearly when there is no compatible protocol version").
 *
 * The ESP-NOW `radioProtocolVersion` is Atlas<->Sigil only and not checked.
 */
object AtlasCompatibility {
    const val API_VERSION = "1"
    const val PROTOCOL_VERSION = "0.1"

    fun requireCompatible(info: AtlasInfo) {
        if (info.apiVersion != API_VERSION) {
            incompatible("HTTP API ${info.apiVersion} (this app needs $API_VERSION)")
        }
        if (info.protocolVersion != PROTOCOL_VERSION) {
            incompatible("protocol ${info.protocolVersion} (this app needs $PROTOCOL_VERSION)")
        }
        if (!info.capabilities.stateSnapshot) {
            incompatible("it does not publish state snapshots")
        }
    }

    fun requireCompatible(snapshot: StateSnapshot) {
        if (snapshot.protocolVersion != PROTOCOL_VERSION) {
            incompatible("state protocol ${snapshot.protocolVersion} (this app needs $PROTOCOL_VERSION)")
        }
    }

    private fun incompatible(detail: String): Nothing =
        throw AtlasException(AtlasFailure.Incompatible(detail))
}
