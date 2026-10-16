package com.turnhub.android.domain

/**
 * What TurnHub keeps for a Pokémon TCG player besides prizes (the score): the
 * once-per-turn actions taken this turn and the Special Conditions on their
 * Active Pokémon. Reminders for the table; card effects stay with the players.
 * Phone-hosted games only until Atlas learns the format.
 */
data class PokemonStatus(
    val turnActions: Set<PokemonAction> = emptySet(),
    val conditions: Set<SpecialCondition> = emptySet(),
) {
    /** Asleep, Confused and Paralyzed replace each other; Burned and Poisoned stack with any. */
    fun withCondition(condition: SpecialCondition, on: Boolean): PokemonStatus = copy(conditions = when {
        !on -> conditions - condition
        condition.rotation -> conditions.filterNot { it.rotation }.toSet() + condition
        else -> conditions + condition
    })
}

/** The once-per-turn actions players most often lose track of. */
enum class PokemonAction(val label: String) {
    ENERGY("Energy attached"),
    SUPPORTER("Supporter played"),
    STADIUM("Stadium played"),
    RETREAT("Retreated"),
}

/** Special Conditions on the Active Pokémon. [rotation] ones turn the card, so only one applies. */
enum class SpecialCondition(val label: String, val rotation: Boolean) {
    ASLEEP("Asleep", true),
    BURNED("Burned", false),
    CONFUSED("Confused", true),
    PARALYZED("Paralyzed", true),
    POISONED("Poisoned", false),
}
