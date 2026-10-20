package com.turnhub.android.domain

/**
 * Death May Die's lower-tier enemies: the Elder One's minions (its cultists and special
 * monster) and the episode's monsters. No stat list is published, so the table types each
 * kind's health and attack dice from its card the first time and the device keeps it
 * ([com.turnhub.android.standalone.LocalLibrary.enemies]) for later games (owner, 2026-10-20).
 */
enum class EnemyGroup(val label: String) { MINION("Elder One minions"), EPISODE("Episode monsters") }

/** One kind of enemy as its card prints it. */
data class EnemyCard(val name: String, val health: Int, val dice: Int, val group: EnemyGroup, val ability: String = "") {
    /** Null when a field is out of range; names are trimmed. */
    fun normalized(): EnemyCard? {
        val clean = name.trim().take(ElderOneTrack.MAX_NAME)
        if (clean.isEmpty() || health !in 1..MAX_HEALTH || dice !in 0..MAX_DICE) return null
        return copy(name = clean, ability = ability.trim().take(MAX_ABILITY))
    }

    companion object {
        const val MAX_HEALTH = 30
        const val MAX_DICE = 12
        const val MAX_ABILITY = 300
        /** Names to start from; the stats always come from the card. */
        val SUGGESTED = listOf("Cultist" to EnemyGroup.MINION, "Cult Master" to EnemyGroup.MINION)
    }
}

/** One figure on the map, numbered among others of its kind ("Cultist 2"), with its wounds. */
data class EnemyFigure(val card: EnemyCard, val number: Int = 1, val wounds: Int = 0) {
    val label: String get() = "${card.name} $number"
    val dead: Boolean get() = wounds >= card.health
}
