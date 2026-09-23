package com.turnhub.android.protocol

/**
 * The Android app's own view of its link to an Atlas.
 *
 * This is a presentation/session concept the app tracks locally, not a field
 * Atlas publishes about itself in protocol/state-v0.1.schema.json. It exists so
 * the UI always has an explicit answer to "are we talking to a table right now,"
 * independent of whatever table state that Atlas eventually reports.
 */
enum class AtlasConnectionState {
    DISCONNECTED,
    CONNECTING,
    CONNECTED,
}
