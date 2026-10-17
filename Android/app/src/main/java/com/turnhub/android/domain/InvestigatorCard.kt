package com.turnhub.android.domain

/** References for cards assigned to an investigator in this match. */
enum class InvestigatorCardSlot(val label: String) {
    SANITY_CONDITION("Sanity condition"),
    RELIC("Relic"),
}

data class InvestigatorCard(val name: String, val rules: String = "", val catalogueId: String? = null) {
    fun normalized(): InvestigatorCard? {
        val entry = InvestigatorCardCatalogue.find(catalogueId)
        val clean = copy(name = name.trim(), rules = rules.trim(),
            catalogueId = entry?.id?.takeIf { entry.name.equals(name.trim(), ignoreCase = true) })
        return clean.takeIf { it.name.isNotEmpty() && it.name.length <= MAX_NAME && it.rules.length <= MAX_RULES }
    }

    companion object {
        const val MAX_NAME = 80
        const val MAX_RULES = 2_000
    }
}
