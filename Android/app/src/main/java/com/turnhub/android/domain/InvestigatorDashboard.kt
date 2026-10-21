package com.turnhub.android.domain

/** Player-owned reference text; each entry adds to (or explicitly replaces) earlier levels. */
data class InvestigatorSkill(val name: String, val levels: List<String> = List(4) { "" })

/** A physical dashboard's markers, independent of turn order and outcomes. */
data class InvestigatorDashboard(
    val insanity: Int = 0,
    val skillLevels: List<Int> = List(3) { 1 },
    val customSkills: List<InvestigatorSkill> = emptyList(),
    val sanityCondition: InvestigatorCard? = null,
    val relic: InvestigatorCard? = null,
    val cardOffers: Map<InvestigatorCardSlot, List<String>> = emptyMap(),
    /** Fire tokens on the investigator board: added when leaving a burning space, rolled and discarded at End of Turn. */
    val fire: Int = 0,
) {
    fun skills(investigator: String?): List<InvestigatorSkill> =
        customSkills.takeIf { it.size in 3..MAX_SKILLS } ?: InvestigatorSkills.forInvestigator(investigator)

    fun fresh(clearCards: Boolean = false): InvestigatorDashboard = copy(insanity = 0, fire = 0, skillLevels = List(3) { 1 },
        customSkills = customSkills.take(3), cardOffers = cardOffers.takeUnless { clearCards }.orEmpty(),
        sanityCondition = sanityCondition.takeUnless { clearCards }, relic = relic.takeUnless { clearCards })

    fun card(slot: InvestigatorCardSlot): InvestigatorCard? = when (slot) {
        InvestigatorCardSlot.SANITY_CONDITION -> sanityCondition
        InvestigatorCardSlot.RELIC -> relic
    }

    /** Sanity loss stops at the next threshold; negative steps correct an entered marker. */
    fun changeInsanity(delta: Int): InvestigatorDashboard {
        val ceiling = THRESHOLDS.firstOrNull { it > insanity } ?: MAX_INSANITY
        val next = (insanity.toLong() + delta).coerceIn(0L, if (delta > 0) ceiling.toLong() else MAX_INSANITY.toLong()).toInt()
        return if (next == insanity) this else copy(insanity = next)
    }

    val bonusDice: Int get() = BONUS_THRESHOLDS.count { insanity >= it }

    /**
     * Skill level-ups earned but not yet taken: every Insanity Threshold reached levels up one
     * skill, after the insanity card activates (rulebook p. 8). Corrections that lower insanity
     * or raise a skill by hand simply bring this back to 0.
     */
    val pendingLevelUps: Int get() =
        (THRESHOLDS.count { insanity >= it } - skillLevels.sumOf { (it - 1).coerceAtLeast(0) }).coerceAtLeast(0)
    val atThreshold: Boolean get() = insanity in THRESHOLDS

    companion object {
        const val MAX_INSANITY = 20
        const val MAX_FIRE = 9
        val THRESHOLDS = listOf(4, 8, 12, 15, 18, 19)
        val BONUS_THRESHOLDS = listOf(8, 15, 18, 19)
        const val MAX_SKILLS = 8
        const val MAX_SKILL_NAME = 60
        const val MAX_LEVEL_TEXT = 600
    }
}

/** Compact paraphrases checked against the printed dashboards reproduced in Make Craft Game's guide. */
object InvestigatorSkills {
    private val known = DmdContent.knownSkills
    val presets: List<InvestigatorSkill> = DmdContent.skillPresets

    fun forInvestigator(name: String?): List<InvestigatorSkill> = known[name]
        ?: InvestigatorIndex.skills(name)?.let { names -> listOf(InvestigatorSkill("Signature skill")) +
            names.map { skillName -> presets.first { it.name == skillName } } }
        ?: List(3) { InvestigatorSkill(if (it == 0) "Signature skill" else "Skill ${it + 1}") }

    fun hasReference(name: String?): Boolean = name in known
}
