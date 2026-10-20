package com.turnhub.android.domain

/** Factual names/links with original, unverified mechanical reminders; no card art or verbatim prose bundled. */
data class InvestigatorCardPreset(
    val id: String,
    val slot: InvestigatorCardSlot,
    val name: String,
    val collection: String,
    val sourceUrl: String,
    val imageId: String? = null,
    val aliases: List<String> = emptyList(),
) {
    fun card() = InvestigatorCardRules.draft(id).let { draft ->
        InvestigatorCard(name, rules = draft?.rules.orEmpty(), catalogueId = id, transformedRules = draft?.transformedRules.orEmpty())
    }
}

/** First community catalogue; custom assignments remain available for missing cards. */
object InvestigatorCardCatalogue {
    const val CONDITIONS_SOURCE = "https://cthulhu-death-may-die.fandom.com/wiki/Insanaties"

    val all: List<InvestigatorCardPreset> = DmdContent.cards

    fun find(id: String?): InvestigatorCardPreset? = all.firstOrNull { it.id == id }

    /** Search both regular and known transformed names, without mixing assignment slots. */
    fun search(slot: InvestigatorCardSlot, query: String): List<InvestigatorCardPreset> {
        val words = query.trim().split(Regex("\\s+")).filter { it.isNotEmpty() }
        return all.filter { entry -> entry.slot == slot && words.all { word ->
            (listOf(entry.name, entry.collection) + entry.aliases).any { it.contains(word, ignoreCase = true) }
        } }.sortedBy { it.name }
    }

    fun imageUrl(imageId: String): String? = images[imageId]
    fun imageSource(imageId: String): String = "https://boardgamegeek.com/image/$imageId"

    // Shared reference photographs may contain paired cards or both relic faces.
    private val images = DmdContent.cardImages
}
