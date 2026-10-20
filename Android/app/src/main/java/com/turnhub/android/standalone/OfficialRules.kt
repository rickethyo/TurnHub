package com.turnhub.android.standalone

import com.turnhub.android.protocol.GameProfile

/**
 * An officially published game mode's fixed setup, from the publisher's rules
 * (Magic: the Comprehensive Rules of 2026-09-25, rule numbers in [summary]).
 * Custom modes (Star Commander, Two-Headed Giant Commander, Other) have none:
 * their life and limits stay editable.
 */
data class OfficialRules(
    val name: String,
    val life: Int,
    /** Poison counters that knock a player (or a 2HG team) out; null when the game has no poison. */
    val poison: Int?,
    /** Combat damage from one commander that knocks a player out; null outside Commander. */
    val commanderDamage: Int?,
    /** Exactly this many players; null for any count the table supports. */
    val players: Int?,
    /** Short rule summaries, each citing the rule it comes from. */
    val summary: List<String>,
    val sourceName: String,
    val sourceUrl: String,
)

/** A mode's rules page, official or custom, for the "?" in setup and the table menu. */
data class ModeRules(val title: String, val official: OfficialRules?, val customSummary: List<String> = emptyList())

private const val MTG_RULES_URL = "https://magic.wizards.com/en/rules"
private const val MTG_SOURCE = "Magic: The Gathering Comprehensive Rules (Wizards of the Coast, 2026-09-25)"

/** The official rules for [profile] (with Two-Headed Giant [teams]); null for a custom mode. */
fun officialRules(profile: GameProfile, teams: Boolean): OfficialRules? = when {
    profile == GameProfile.MTG && teams -> OfficialRules(
        "Two-Headed Giant", life = 30, poison = 15, commanderDamage = null, players = 4,
        summary = listOf(
            "Two teams of two players each (810.1). Teammates sit together (810.3).",
            "Each team shares one life total that starts at 30 (810.4).",
            "Teams take turns together; the team that goes first skips its first draw (810.2, 810.6).",
            "Damage, life loss and life gain happen to each player, and the result applies to the team's total (810.9).",
            "Poison counters are shared by the team; a team with 15 or more loses (810.10, 810.8d).",
            "Other resources, such as cards in hand and mana, are not shared (810.5).",
            "Teams win and lose together. If one player concedes, their team leaves and loses (810.8a, 810.8b).",
        ),
        sourceName = MTG_SOURCE, sourceUrl = MTG_RULES_URL,
    )
    profile == GameProfile.MTG -> OfficialRules(
        "Magic", life = 20, poison = 10, commanderDamage = null, players = null,
        summary = listOf(
            "Each player starts at 20 life (103.4).",
            "A player with 0 or less life loses (104.3b).",
            "A player with ten or more poison counters loses (104.3d).",
            "A player who concedes leaves the game and loses (104.3a).",
        ),
        sourceName = MTG_SOURCE, sourceUrl = MTG_RULES_URL,
    )
    profile == GameProfile.MTG_COMMANDER && !teams -> OfficialRules(
        "Commander", life = 40, poison = 10, commanderDamage = 21, players = null,
        summary = listOf(
            "Each player starts at 40 life (903.7).",
            "Multiplayer Commander is free-for-all by default (903.2).",
            "A player dealt 21 or more combat damage by the same commander loses. Each commander is counted separately (903.10a).",
            "A commander cast from the command zone costs {2} more for each previous cast from there (903.8).",
            "A player with 0 or less life, or ten or more poison counters, loses (104.3b, 104.3d).",
        ),
        sourceName = MTG_SOURCE, sourceUrl = MTG_RULES_URL,
    )
    profile == GameProfile.YUGIOH -> OfficialRules(
        "Yu-Gi-Oh!", life = 8000, poison = null, commanderDamage = null, players = 2,
        summary = listOf(
            "A Duel is between two players, each starting with 8000 Life Points.",
            "A player whose Life Points reach 0 loses the Duel.",
        ),
        sourceName = "Yu-Gi-Oh! TRADING CARD GAME rulebook (Konami)",
        sourceUrl = "https://www.yugioh-card.com/en/rulebook/",
    )
    profile == GameProfile.POKEMON -> OfficialRules(
        "Pokémon", life = 6, poison = null, commanderDamage = null, players = 2,
        summary = listOf(
            "A game is between two players. Each sets aside 6 Prize cards; TurnHub counts the Prize cards you have left.",
            "When you Knock Out an opponent's Pokémon you take Prize cards (2 for a Pokémon ex, as the card says). Take your last one and you win.",
            "You also win when your opponent has no Pokémon left in play, or can't draw a card at the start of their turn.",
            "The player who goes first can't attack or play a Supporter card on their first turn.",
            "Each turn you may attach one Energy, play one Supporter, play one Stadium and retreat once.",
            "Between turns, Pokémon Checkup: Poisoned puts 1 damage counter on the Active Pokémon; Burned puts 2, then flip a coin " +
                "and heads removes it; an Asleep Pokémon flips and wakes on heads; Paralyzed ends after its owner's next turn.",
        ),
        sourceName = "Pokémon Trading Card Game rulebook (The Pokémon Company International)",
        sourceUrl = "https://www.pokemon.com/us/pokemon-tcg/rules",
    )
    profile == GameProfile.LORCANA -> OfficialRules(
        "Lorcana", life = 20, poison = null, commanderDamage = null, players = null,
        summary = listOf(
            "Each player starts with 0 lore. The first player to reach 20 lore wins at once (TurnHub counts lore up; the goal is the number in setup).",
            "A player who has to draw from an empty deck loses.",
            "Games with three or more players follow the same rules. A player who loses leaves the game, and the last player left wins.",
            "The first player skips their draw on their first turn.",
            "Once per turn you may put a card with the inkwell symbol into your inkwell.",
        ),
        sourceName = "Disney Lorcana Comprehensive Rules (Ravensburger)",
        sourceUrl = "https://www.disneylorcana.com/en-US/resources",
    )
    else -> null
}

/** The rules page for a mode: official rules, or what a custom mode changes. */
fun modeRules(profile: GameProfile, teams: Boolean): ModeRules = officialRules(profile, teams)?.let { ModeRules(it.name, it) }
    ?: when {
        profile.deathMayDie -> ModeRules("Cthulhu: Death May Die", null, listOf(
            "A cooperative game: the investigators win or lose together.",
            "Each investigator takes one turn a round, in whatever order the table likes. Take the next turn stages an investigator as Up next; their turn starts when the current player passes. With nobody staged, Pass asks who goes next.",
            "The investigators win by killing the Elder One in its final stage.",
            "If an investigator dies or goes insane before the Elder One is summoned, the game is lost. Once it is on the board, the others play on.",
            "The game is also lost when the Summoning track reaches its last space, or when every investigator is dead or insane.",
            "Each panel counts that player's wounds as a reminder; their dashboard decides when they die.",
            "The Elder One tab follows the Story board: three Summoning symbols in the Mythos discards advance the Elder One, entering the first red space (space 5) or disrupting the ritual summons it, and the Progression token reaching space 8 loses the game.",
            "Once the ritual is disrupted, wound the Elder One stage by stage. Excess wounds don't carry over, and defeating the Final stage wins.",
            "With the Elder One chosen in that tab, an investigator lost while it is still on the Summoning track ends the game as a loss.",
        ))
        profile.bagOfDoom -> ModeRules("Commander · Bag of Doom (custom)", null, listOf(
            "A custom Commander variant for five or more players, inspired by Game Knights' ten-player game. Start at 40 life; normal Commander rules apply.",
            "Everyone begins in the bag. When another player loses life on your turn, your name comes out, even if they paid that life themselves. Your own life loss does not save you.",
            "The first bag check follows the fourth full turn cycle. Then check once per cycle. A random name still in the bag is eliminated; an empty bag eliminates nobody. Each check refills the bag.",
            "Crediting an elimination refills the bag except for that player and moves the boundary to the living player before them. The new check is after one cycle at that boundary, including during the opening grace period.",
            "Use the bag panel to correct missed life entries or credit an automatic removal. Concessions and bag-drawn players give no elimination credit. At two survivors the bag switches off.",
            "A bag check pauses play. Resolve pending life approvals, review entries, then Draw. The saved result cannot be rerolled by reopening the panel. Continue resumes the next turn.",
            "TurnHub uses the community summary's reset-every-check interpretation. Episode: https://www.youtube.com/watch?v=CvyRDmwOeCs ; summary: https://www.reddit.com/r/EDH/comments/1sng1au/commandzones_10_player_game/",
        ))
        profile.starCommander -> ModeRules("Star Commander (custom)", null, listOf(
            "A five-player Commander variant played by many groups; it is not part of Magic's official rules.",
            "Each player starts at 40 life.",
            "Your two neighbors are your allies and can't deal you Commander damage.",
            "The two players across the star are your enemies. You win once both are out, and allies can share the win.",
            "Otherwise Commander's rules apply: 21 combat damage from one commander knocks a player out.",
        ))
        profile == GameProfile.MTG_COMMANDER && teams -> ModeRules("Two-Headed Giant Commander (custom)", null, listOf(
            "Magic's official rules don't cover Two-Headed Giant Commander, so this is a house variant.",
            "Teammates share one life total (60 by default) and one poison count (15), as in Two-Headed Giant.",
            "Commander damage is still counted for each player separately. A player dealt 21 by one commander is out, and so is their team.",
        ))
        else -> ModeRules("Custom game", null, listOf(
            "No official rules apply. Choose the starting life and limits your group plays with.",
        ))
    }
