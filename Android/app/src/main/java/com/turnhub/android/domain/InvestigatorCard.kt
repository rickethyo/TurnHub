package com.turnhub.android.domain

/** Optional, player-entered references for cards assigned to an investigator in this match. */
enum class InvestigatorCardSlot(val label: String) {
    SANITY_CONDITION("Sanity condition"),
    RELIC("Relic"),
}

data class InvestigatorCard(val name: String, val rules: String = "") {
    fun normalized(): InvestigatorCard? {
        val clean = copy(name = name.trim(), rules = rules.trim())
        return clean.takeIf { it.name.isNotEmpty() && it.name.length <= MAX_NAME && it.rules.length <= MAX_RULES }
    }

    companion object {
        const val MAX_NAME = 80
        const val MAX_RULES = 2_000
    }
}
