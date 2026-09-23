package com.turnhub.android.protocol

/**
 * Mirrors `settings.profile` in protocol/state-v0.1.schema.json
 * ("generic", "mtg", "mtg_commander", "yugioh"). See
 * Documentation/engineering/GAME_PROFILES_AND_LIFE.md for what each format means
 * (starting life presets, etc.) -- this app does not implement or validate rules
 * for any of them, it only displays Atlas's current selection.
 */
enum class GameProfile {
    GENERIC,
    MTG,
    MTG_COMMANDER,
    YUGIOH,
}

/**
 * Mirrors the `settings` object in protocol/state-v0.1.schema.json: the game
 * format and starting life Atlas captured for the current match.
 */
data class TableSettings(
    val profile: GameProfile,
    val startingLife: Int,
)
