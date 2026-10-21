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
    MTG_BAG_OF_DOOM("mtg_bag_of_doom"),
    /**
     * Emperor (CR 809): two teams of three, each emperor flanked by two generals,
     * with limited range of influence; a team wins when the rival emperor is out
     * ([com.turnhub.android.domain.Emperor]). Magic decks at 20 life. Phone-hosted
     * games only, like [POKEMON].
     */
    MTG_EMPEROR("mtg_emperor"),
    /** [MTG_EMPEROR] with Commander decks: 40 life and Commander damage (a house combination). */
    MTG_COMMANDER_EMPEROR("mtg_commander_emperor"),
    YUGIOH("yugioh"),
    /**
     * Pokémon TCG: each player counts the Prize cards they have left, from 6;
     * taking the last one wins. Phone-hosted games only: Atlas doesn't know it yet.
     */
    POKEMON("pokemon"),
    /**
     * Disney Lorcana: lore counts up from 0 and the first player to reach the
     * goal (20) wins at once. Phone-hosted games only, like [POKEMON].
     */
    LORCANA("lorcana"),
    /**
     * Cthulhu: Death May Die, a cooperative board game: the investigators win
     * or lose together. TurnHub tracks who has taken their turn this round,
     * each player's investigator and their wounds. Phone-hosted games only, like [POKEMON].
     */
    DEATH_MAY_DIE("death_may_die"),
    ;

    /** Commander rules (Commander damage, commander casts, 40 life): Commander and its variants. */
    val commanderRules: Boolean get() = this == MTG_COMMANDER || this == MTG_STAR_COMMANDER || this == MTG_BAG_OF_DOOM ||
        this == MTG_COMMANDER_EMPEROR

    /** Magic rules (poison, energy, monarch, initiative...): every Magic format. */
    val magicRules: Boolean get() = this == MTG || this == MTG_EMPEROR || commanderRules

    val bagOfDoom: Boolean get() = this == MTG_BAG_OF_DOOM

    val starCommander: Boolean get() = this == MTG_STAR_COMMANDER

    /** Emperor, with Magic or Commander decks (see [MTG_EMPEROR]). */
    val emperor: Boolean get() = this == MTG_EMPEROR || this == MTG_COMMANDER_EMPEROR

    /** The format chip a variant belongs to: Magic for [MTG_EMPEROR], Commander for the Commander variants. */
    val baseFormat: GameProfile get() = when (this) {
        MTG_EMPEROR -> MTG
        MTG_STAR_COMMANDER, MTG_BAG_OF_DOOM, MTG_COMMANDER_EMPEROR -> MTG_COMMANDER
        else -> this
    }

    /** A format with its own fixed seating and teams: no Two-Headed Giant on top of it. */
    val ownSeating: Boolean get() = starCommander || bagOfDoom || emperor

    /** Prizes instead of life (see [POKEMON]). */
    val pokemon: Boolean get() = this == POKEMON

    /** Lore instead of life (see [LORCANA]); the settings' starting life is the lore to win. */
    val lorcana: Boolean get() = this == LORCANA

    /** Wounds instead of life (see [DEATH_MAY_DIE]); the whole table wins or loses. */
    val deathMayDie: Boolean get() = this == DEATH_MAY_DIE

    /** The players win or lose together. */
    val cooperative: Boolean get() = deathMayDie

    /** A life total that can run out; Pokémon counts prizes, Lorcana lore and Death May Die wounds instead. */
    val countsLife: Boolean get() = !pokemon && !lorcana && !deathMayDie

    /** Atlas runs and imports this format; the others are phone-hosted only. */
    val atlasSupported: Boolean get() = this != POKEMON && this != LORCANA && this != DEATH_MAY_DIE && !emperor

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
