package com.turnhub.android.protocol

/**
 * A game's phase ("LOBBY", "STARTING", "RUNNING", "PAUSED", "GAME_OVER"), as
 * the shared core names it and `hub.state` in protocol/atlas-state-v1.schema.json
 * reports it. The hub that runs the game owns it (Documentation/engineering/
 * ARCHITECTURAL_INVARIANTS.md, Invariant 1).
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
