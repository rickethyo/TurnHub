package com.turnhub.android.protocol

/**
 * Mirrors the `state` enum in protocol/state-v0.1.schema.json
 * ("LOBBY", "STARTING", "RUNNING", "PAUSED", "GAME_OVER").
 *
 * Atlas is the sole owner of this value (Documentation/engineering/
 * ARCHITECTURAL_INVARIANTS.md, Invariant 1); this app only ever renders it.
 * A match Atlas restored after a reboot arrives as [PAUSED] like any other
 * paused game.
 */
enum class TableState {
    LOBBY,
    STARTING,
    RUNNING,
    PAUSED,
    GAME_OVER,
    ;

    companion object {
        fun fromWire(value: String): TableState? = entries.firstOrNull { it.name == value }
    }
}
