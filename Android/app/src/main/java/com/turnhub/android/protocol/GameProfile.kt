package com.turnhub.android.protocol

/**
 * Mirrors `settings.profile` in protocol/state-v0.1.schema.json. See
 * Documentation/engineering/GAMEPLAY.md for what each format means
 * (starting life presets, etc.) -- this app does not implement or validate rules
 * for any of them, it only displays Atlas's current selection.
 */
enum class GameProfile(val wireValue: String) {
    GENERIC("generic"),
    MTG("mtg"),
    MTG_COMMANDER("mtg_commander"),
    /**
     * Star Commander: a five-player Commander variant. Each seat's two
     * neighbours are allies and the two across the star are enemies; a player
     * wins once both enemies are out ([com.turnhub.android.domain.StarCommander]).
     */
    MTG_STAR_COMMANDER("mtg_star_commander"),
    YUGIOH("yugioh"),
    /**
     * Pokémon TCG: each player counts the Prize cards they have left, from 6;
     * taking the last one wins. Phone-hosted games only: Atlas doesn't know it yet.
     */
    POKEMON("pokemon"),
    ;

    /** Commander rules (Commander damage, commander casts, 40 life): Commander and Star Commander. */
    val commanderRules: Boolean get() = this == MTG_COMMANDER || this == MTG_STAR_COMMANDER

    /** Magic rules (poison, energy, monarch, initiative...): every Magic format. */
    val magicRules: Boolean get() = this == MTG || commanderRules

    val starCommander: Boolean get() = this == MTG_STAR_COMMANDER

    /** Prizes instead of life (see [POKEMON]). */
    val pokemon: Boolean get() = this == POKEMON

    /** Atlas runs and imports this format; the others are phone-hosted only. */
    val atlasSupported: Boolean get() = this != POKEMON

    companion object {
        fun fromWire(value: String): GameProfile? = entries.firstOrNull { it.wireValue == value }
    }
}

/**
 * Mirrors the `settings` object in protocol/state-v0.1.schema.json: the game
 * format, starting life and turn timer Atlas captured for the current match
 * (or, in the lobby, the setup the next match will capture).
 */
data class TableSettings(
    val profile: GameProfile,
    val startingLife: Int,
    /** Per-turn countdown; 0 = OFF. Firmware before the turn timer omits it (OFF). */
    val turnTimerMs: Long = 0,
    /**
     * Two-Headed Giant (Magic and Commander only): neighbours in turn order are
     * teams of two that share one life total and one turn. [startingLife] is
     * the team's total. Older firmware omits it (false).
     */
    val twoHeadedGiant: Boolean = false,
    /** Older firmware omits this setting and never auto-eliminates. */
    val autoEliminateAtZeroLife: Boolean = false,
    val autoEliminateAtPoisonLimit: Boolean = false,
    val autoEliminateAtCommanderDamage: Boolean = false,
    val poisonLimit: Int = if (twoHeadedGiant) 15 else 10,
    /** House rules unlock an official mode's life, limits and player count (the standalone game and Atlas alike). */
    val houseRules: Boolean = false,
) {
    val turnTimerEnabled: Boolean get() = turnTimerMs > 0
}

/**
 * Mirrors `turnTimer` in protocol/state-v0.1.schema.json. Atlas derives the
 * phase; the app only displays it. A sampled value like the clocks: it can
 * change without a revision change.
 */
data class TurnTimer(
    val phase: TurnTimerPhase,
    /** Countdown left when the timer is on and a turn is running; otherwise null. */
    val remainingMs: Long?,
)

enum class TurnTimerPhase(val wireValue: String) {
    /** Nothing to show. */
    NORMAL("NORMAL"),

    /** Ten seconds or less left. */
    WARNING("WARNING"),

    /** The countdown ran out. The turn continues; Atlas never passes it. */
    EXPIRED("EXPIRED"),

    /** Timer OFF and the turn has lasted five minutes: a gentle cue only. */
    LONG_TURN("LONG_TURN"),
    ;

    companion object {
        fun fromWire(value: String): TurnTimerPhase? = entries.firstOrNull { it.wireValue == value }
    }
}

/**
 * `GET /api/game/settings`: the table's next-match setup, whether this session
 * may edit it (host, in the lobby) and the turn-timer choices Atlas offers.
 */
data class GameSettingsInfo(
    val settings: TableSettings,
    val available: Boolean,
    val canEdit: Boolean,
    val turnTimerPresetsMs: List<Long>,
    val turnTimerMinMs: Long,
    val turnTimerMaxMs: Long,
)
