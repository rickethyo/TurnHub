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
) {
    fun skills(investigator: String?): List<InvestigatorSkill> =
        customSkills.takeIf { it.size == 3 } ?: InvestigatorSkills.forInvestigator(investigator)

    fun fresh(clearCards: Boolean = false): InvestigatorDashboard = copy(insanity = 0, skillLevels = List(3) { 1 },
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
    val atThreshold: Boolean get() = insanity in THRESHOLDS

    companion object {
        const val MAX_INSANITY = 20
        val THRESHOLDS = listOf(4, 8, 12, 15, 18, 19)
        val BONUS_THRESHOLDS = listOf(8, 15, 18, 19)
        const val MAX_SKILL_NAME = 60
        const val MAX_LEVEL_TEXT = 600
    }
}

/** Compact paraphrases checked against the printed dashboards reproduced in Make Craft Game's guide. */
object InvestigatorSkills {
    private fun skill(name: String, vararg levels: String) = InvestigatorSkill(name, levels.toList())
    private val stealth = skill("Stealth", "During a Run, Sneak past 1 enemy so it does not follow.",
        "Sneak up to 3 times per Run instead.", "Deal 1 wound to each enemy you Sneak past.", "Sneak any number of times per Run instead.")
    private val swiftness = skill("Swiftness", "Move 1 extra space when you Run.", "Gain 1 free Run action each turn.",
        "When leaving a space during a Run, you may bring 1 investigator with you.", "Gain 1 extra action each turn.")
    private val marksman = skill("Marksman", "You may attack a target 1 space away.",
        "Add 2 green dice when attacking outside your space.", "You may attack up to 2 spaces away instead.",
        "Gain 1 free attack each turn against a target outside your space.")
    private val arcane = skill("Arcane Mastery", "On any roll, you may count 1 Elder Sign as a success.",
        "Count any number of Elder Signs as successes instead.", "Heal 1 stress for each Elder Sign counted as a success.",
        "Each Elder Sign you count is worth 2 successes instead.")
    private val brawling = skill("Brawling", "Add 1 green die when attacking in your space.",
        "You may target any number of enemies in your space, dividing wounds between them.",
        "Gain 2 free rerolls when attacking in your space.", "Deal the full wounds to each target in your space instead of dividing them.")
    private val toughness = skill("Toughness", "Gain 1 free reroll when attacked or rolling for Fire.",
        "Instead, reduce wounds and sanity loss by 1 when attacked or rolling for Fire.",
        "Instead, reduce wounds and sanity loss by 1 from any source.",
        "Instead, reduce wounds by 2 and sanity loss by 1 from any source.")
    private val savage = skill("Savage", "An attack deals 1 extra wound if the target is the only enemy in its space.",
        "Deal 2 extra wounds in total instead.", "If you kill that lone target, heal all your stress.", "Deal 5 extra wounds in total instead.")
    private val lucky = skill("Lucky", "Gain 1 free reroll per turn.", "Gain 2 free rerolls per turn instead.",
        "Gain 3 free rerolls per turn instead.", "Gain 3 free rerolls for every roll instead.")

    private val known = mapOf(
        "Lord Adam Benchley" to listOf(skill("Fueled by Madness",
            "Add 1 green die while your sanity marker is on an insanity threshold.",
            "Instead, add 1 green die on a threshold or the space immediately before it.",
            "Instead, add 2 green dice on a threshold or the space immediately before it.",
            "Instead, add 3 green dice on a threshold or the space immediately before it."), marksman, toughness),
        "Ahmed Yasin" to listOf(skill("Healing Prayer",
            "At turn end, heal 1 stress or 1 wound on an investigator in your space, including yourself.",
            "Instead, heal 2 in any combination of stress and wounds on that investigator.",
            "Instead, heal 2 stress and 2 wounds on that investigator.",
            "Instead, heal 2 stress and 2 wounds on every investigator in your space."), stealth, arcane),
        "Borden" to listOf(savage, swiftness, stealth),
        "Elizabeth Ives" to listOf(lucky, marksman, stealth),
        "Fatima Safar" to listOf(skill("Read the Omens",
            "You may put your first Mythos draw each turn under the deck and draw again; doing so heals 2 stress.",
            "Instead, draw 2 Mythos cards, choose 1 to play, put the other under the deck, and heal 2 stress.",
            "Instead, choosing between 2 Mythos cards heals all your stress.",
            "You may take 2 wounds to cancel a Mythos card's special effects and enemy summoning."), arcane, swiftness),
        "Sergeant Ian Welles" to listOf(skill("Vengeance Obsession",
            "When an attack wounds you, deal 1 wound to the attacker and heal 1 stress.",
            "Instead, deal 1 wound to each enemy in the attacker's space and heal 1 stress.",
            "Instead, deal 1 wound to each enemy in the attacker's space and heal 2 stress.",
            "Instead, deal 2 wounds to each enemy in the attacker's space and heal 2 stress."), brawling, swiftness),
        "John Morgan" to listOf(skill("Protector",
            "Before an attack roll against an investigator in your space, you may redirect the attack to yourself.",
            "You may also redirect attacks against an investigator up to 1 space away.",
            "Gain 2 free rerolls when defending against a redirected attack.",
            "After a redirected attack resolves, heal all your stress."), swiftness, toughness),
        "Rasputin" to listOf(skill("Unkillable",
            "Once per game, dying from wounds returns you to life at full health instead.",
            "You have 2 such returns to life in total instead.", "Returning to life also heals all your stress.",
            "You have 3 such returns to life in total instead."), arcane, brawling),
        "Sister Beth" to listOf(skill("High Strung",
            "On any roll, count 1 tentacle as a success; it still causes sanity loss.",
            "Count any number of tentacles as successes instead; they still cause sanity loss.",
            "Heal 1 wound for each tentacle counted as a success.",
            "Rolling at least 2 tentacles also lets you add 3 successes."), brawling, toughness),
        "The Kid" to listOf(skill("Gate Manipulation",
            "While you share a Gate's space, monsters and Cultists summoned there take wounds equal to half their health, rounded up.",
            "The summoning effect also works while you are within 1 space of a Gate.",
            "Within 1 space of a Gate, you are always safe to Rest.",
            "Within 1 space of a Gate, add 1 green die and gain 1 free Rest action."), marksman, arcane),
    )

    fun forInvestigator(name: String?): List<InvestigatorSkill> = known[name]
        ?: List(3) { InvestigatorSkill(if (it == 0) "Signature skill" else "Skill ${it + 1}") }

    fun hasReference(name: String?): Boolean = name in known
}
