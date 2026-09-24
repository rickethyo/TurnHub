package com.turnhub.android.protocol

/**
 * Mirrors `settings.profile` in protocol/state-v0.1.schema.json. See
 * Documentation/engineering/GAME_PROFILES_AND_LIFE.md for what each format means
 * (starting life presets, etc.) -- this app does not implement or validate rules
 * for any of them, it only displays Atlas's current selection.
 */
enum class GameProfile(val wireValue: String) {
    GENERIC("generic"),
    MTG("mtg"),
    MTG_COMMANDER("mtg_commander"),
    YUGIOH("yugioh"),
    ;

    companion object {
        fun fromWire(value: String): GameProfile? = entries.firstOrNull { it.wireValue == value }
    }
}

/**
 * Mirrors the `settings` object in protocol/state-v0.1.schema.json: the game
 * format and starting life Atlas captured for the current match (or, in the
 * lobby, the setup the next match will capture).
 */
data class TableSettings(
    val profile: GameProfile,
    val startingLife: Int,
)
