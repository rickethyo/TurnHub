package com.turnhub.android.domain

import com.turnhub.android.protocol.GameProfile

/**
 * What TurnHub keeps for a player in the card games beyond Magic (Pokémon,
 * Lorcana), besides the panel's score: the once-per-turn actions taken this
 * turn and (Pokémon) the Special Conditions on their Active Pokémon. Reminders
 * for the table; card effects stay with the players. Phone-hosted games only
 * until Atlas learns these formats.
 */
data class TurnStatus(
    val turnActions: Set<TurnAction> = emptySet(),
    val conditions: Set<SpecialCondition> = emptySet(),
) {
    /** Asleep, Confused and Paralyzed replace each other; Burned and Poisoned stack with any. */
    fun withCondition(condition: SpecialCondition, on: Boolean): TurnStatus = copy(conditions = when {
        !on -> conditions - condition
        condition.rotation -> conditions.filterNot { it.rotation }.toSet() + condition
        else -> conditions + condition
    })
}

/** The once-per-turn actions players most often lose track of, by game. */
enum class TurnAction(val label: String, val profile: GameProfile) {
    ENERGY("Energy attached", GameProfile.POKEMON),
    SUPPORTER("Supporter played", GameProfile.POKEMON),
    STADIUM("Stadium played", GameProfile.POKEMON),
    RETREAT("Retreated", GameProfile.POKEMON),
    INK("Card inked", GameProfile.LORCANA),
    ;

    companion object {
        fun of(profile: GameProfile): List<TurnAction> = entries.filter { it.profile == profile }
    }
}

/** Pokémon Special Conditions on the Active Pokémon. [rotation] ones turn the card, so only one applies. */
enum class SpecialCondition(val label: String, val rotation: Boolean) {
    ASLEEP("Asleep", true),
    BURNED("Burned", false),
    CONFUSED("Confused", true),
    PARALYZED("Paralyzed", true),
    POISONED("Poisoned", false),
}
