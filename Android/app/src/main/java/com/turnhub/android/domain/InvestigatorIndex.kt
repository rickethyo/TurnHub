package com.turnhub.android.domain

import kotlin.random.Random

/** Factual common-skill associations and reference links, checked 2026-10-17.
 * Source: Make Craft Game's Death May Die index. No source code, card art or rules copied.
 * Spellings are mapped to TurnHub's existing investigator identities.
 */
object InvestigatorIndex {
    const val SOURCE = "https://makecraftgame.com/death-may-die/"
    private val common = DmdContent.commonSkills
    fun skills(name: String?): List<String>? = common[name]
    val filters = DmdContent.skillFilters

    fun matches(name: String, query: String, skill: String?): Boolean {
        val names = InvestigatorSkills.forInvestigator(name).map { it.name }
        return (skill == null || names.any { it.equals(skill, ignoreCase = true) }) &&
            (name.contains(query.trim(), ignoreCase = true) || names.any { it.contains(query.trim(), ignoreCase = true) })
    }

    fun random(query: String, skill: String?, taken: Set<String>, random: Random = Random.Default): String? =
        DeathMayDie.all.filter { name -> matches(name, query, skill) && taken.none { it.equals(name, ignoreCase = true) } }
            .randomOrNull(random)
}
