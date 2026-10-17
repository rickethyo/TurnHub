package com.turnhub.android.domain

/** Factual names and links only. No card rules or artwork are bundled. */
data class InvestigatorCardPreset(
    val id: String,
    val slot: InvestigatorCardSlot,
    val name: String,
    val collection: String,
    val sourceUrl: String,
    val imageId: String? = null,
    val aliases: List<String> = emptyList(),
) {
    fun card() = InvestigatorCard(name, catalogueId = id)
}

/** First community catalogue; custom assignments remain available for missing cards. */
object InvestigatorCardCatalogue {
    const val CONDITIONS_SOURCE = "https://cthulhu-death-may-die.fandom.com/wiki/Insanaties"
    private const val FOTU_SOURCE = "https://boardgamegeek.com/boardgame/373600/cthulhu-death-may-die-fear-of-the-unknown"
    private const val S4_SOURCE = "https://boardgamegeek.com/image/7193296/cthulhu-death-may-die-season-4-expansion"
    private const val BONUS_SOURCE = "https://boardgamegeek.com/boardgame/375371/cthulhu-death-may-die-bonus-relic-cards"

    val all: List<InvestigatorCardPreset> = buildList {
        add(InvestigatorCardPreset("condition:autophobia", InvestigatorCardSlot.SANITY_CONDITION, "Autophobia", "Fear of the Unknown", FOTU_SOURCE, "7194666"))
        add(InvestigatorCardPreset("condition:bloodlust", InvestigatorCardSlot.SANITY_CONDITION, "Bloodlust", "Fear of the Unknown", FOTU_SOURCE, "7194667"))
        add(InvestigatorCardPreset("condition:catatonia", InvestigatorCardSlot.SANITY_CONDITION, "Catatonia", "Community conditions", CONDITIONS_SOURCE))
        add(InvestigatorCardPreset("condition:chronophobia", InvestigatorCardSlot.SANITY_CONDITION, "Chronophobia", "Community conditions", CONDITIONS_SOURCE))
        add(InvestigatorCardPreset("condition:claustrophobia", InvestigatorCardSlot.SANITY_CONDITION, "Claustrophobia", "Community conditions", CONDITIONS_SOURCE))
        add(InvestigatorCardPreset("condition:codependency", InvestigatorCardSlot.SANITY_CONDITION, "Codependency", "Community conditions", CONDITIONS_SOURCE, null, listOf("Co-dependent", "Codependent")))
        add(InvestigatorCardPreset("condition:compulsive-accumulating", InvestigatorCardSlot.SANITY_CONDITION, "Compulsive Accumulating", "Fear of the Unknown", FOTU_SOURCE, "7194665"))
        add(InvestigatorCardPreset("condition:conspiratorial-thinking", InvestigatorCardSlot.SANITY_CONDITION, "Conspiratorial Thinking", "Community conditions", CONDITIONS_SOURCE))
        add(InvestigatorCardPreset("condition:dissociative-identity", InvestigatorCardSlot.SANITY_CONDITION, "Dissociative Identity", "Community conditions", CONDITIONS_SOURCE))
        add(InvestigatorCardPreset("condition:eyes-everywhere", InvestigatorCardSlot.SANITY_CONDITION, "Eyes Everywhere", "Community conditions", CONDITIONS_SOURCE))
        add(InvestigatorCardPreset("condition:fear-of-crowds", InvestigatorCardSlot.SANITY_CONDITION, "Fear of Crowds", "Community conditions", CONDITIONS_SOURCE, null, listOf("Enochlophobia")))
        add(InvestigatorCardPreset("condition:flame-wings", InvestigatorCardSlot.SANITY_CONDITION, "Flame Wings", "Community conditions", CONDITIONS_SOURCE))
        add(InvestigatorCardPreset("condition:hallucinations", InvestigatorCardSlot.SANITY_CONDITION, "Hallucinations", "Fear of the Unknown", FOTU_SOURCE, "7194667"))
        add(InvestigatorCardPreset("condition:hypochondria", InvestigatorCardSlot.SANITY_CONDITION, "Hypochondria", "Community conditions", CONDITIONS_SOURCE))
        add(InvestigatorCardPreset("condition:hyperactivity", InvestigatorCardSlot.SANITY_CONDITION, "Hyperactivity", "Community conditions", CONDITIONS_SOURCE, null, listOf("Hypeactivity")))
        add(InvestigatorCardPreset("condition:impulsive-aggression", InvestigatorCardSlot.SANITY_CONDITION, "Impulsive Aggression", "Fear of the Unknown", FOTU_SOURCE, "7194666", listOf("Impulsive Agression")))
        add(InvestigatorCardPreset("condition:insomnia", InvestigatorCardSlot.SANITY_CONDITION, "Insomnia", "Community conditions", CONDITIONS_SOURCE))
        add(InvestigatorCardPreset("condition:kleptomania", InvestigatorCardSlot.SANITY_CONDITION, "Kleptomania", "Community conditions", CONDITIONS_SOURCE))
        add(InvestigatorCardPreset("condition:masochism", InvestigatorCardSlot.SANITY_CONDITION, "Masochism", "Community conditions", CONDITIONS_SOURCE))
        add(InvestigatorCardPreset("condition:megalomania", InvestigatorCardSlot.SANITY_CONDITION, "Megalomania", "Fear of the Unknown", FOTU_SOURCE, "7194665"))
        add(InvestigatorCardPreset("condition:monomania", InvestigatorCardSlot.SANITY_CONDITION, "Monomania", "Community conditions", CONDITIONS_SOURCE))
        add(InvestigatorCardPreset("condition:narcolepsy", InvestigatorCardSlot.SANITY_CONDITION, "Narcolepsy", "Community conditions", CONDITIONS_SOURCE))
        add(InvestigatorCardPreset("condition:obsessive-disorder", InvestigatorCardSlot.SANITY_CONDITION, "Obsessive Disorder", "Community conditions", CONDITIONS_SOURCE, null, listOf("OCD", "Obsessive Compulsive Disorder")))
        add(InvestigatorCardPreset("condition:overcaring", InvestigatorCardSlot.SANITY_CONDITION, "Overcaring", "Fear of the Unknown", FOTU_SOURCE, "7194664"))
        add(InvestigatorCardPreset("condition:paranoia", InvestigatorCardSlot.SANITY_CONDITION, "Paranoia", "Community conditions", "https://filipe-pagliuso.artstation.com/projects/RY3gaE"))
        add(InvestigatorCardPreset("condition:pyromania", InvestigatorCardSlot.SANITY_CONDITION, "Pyromania", "Community conditions", "https://filipe-pagliuso.artstation.com/projects/RY3gaE"))
        add(InvestigatorCardPreset("condition:r-lyehian", InvestigatorCardSlot.SANITY_CONDITION, "R’lyehian", "Community conditions", CONDITIONS_SOURCE, null, listOf("R'Lyehian", "Rlyehian")))
        add(InvestigatorCardPreset("condition:recurring-trauma", InvestigatorCardSlot.SANITY_CONDITION, "Recurring Trauma", "Community conditions", "https://www.cmon.com/wp-content/uploads/2023/06/DMD006-Rulebook.pdf"))
        add(InvestigatorCardPreset("condition:restless-limbs", InvestigatorCardSlot.SANITY_CONDITION, "Restless Limbs", "Community conditions", CONDITIONS_SOURCE))
        add(InvestigatorCardPreset("condition:sadism", InvestigatorCardSlot.SANITY_CONDITION, "Sadism", "Community conditions", CONDITIONS_SOURCE))
        add(InvestigatorCardPreset("condition:shared-psychosis", InvestigatorCardSlot.SANITY_CONDITION, "Shared Psychosis", "Community conditions", CONDITIONS_SOURCE))
        add(InvestigatorCardPreset("condition:short-term-memory-loss", InvestigatorCardSlot.SANITY_CONDITION, "Short-Term Memory Loss", "Community conditions", CONDITIONS_SOURCE))
        add(InvestigatorCardPreset("condition:tentacle-limbs", InvestigatorCardSlot.SANITY_CONDITION, "Tentacle Limbs", "Community conditions", CONDITIONS_SOURCE))
        add(InvestigatorCardPreset("condition:traveling-fugue", InvestigatorCardSlot.SANITY_CONDITION, "Traveling Fugue", "Fear of the Unknown", FOTU_SOURCE, "7194664"))
        add(InvestigatorCardPreset("condition:uncontrollable-fear", InvestigatorCardSlot.SANITY_CONDITION, "Uncontrollable Fear", "Community conditions", CONDITIONS_SOURCE))
        add(InvestigatorCardPreset("relic:ancient-idol", InvestigatorCardSlot.RELIC, "Ancient Idol", "Fear of the Unknown", FOTU_SOURCE))
        add(InvestigatorCardPreset("relic:brass-compass", InvestigatorCardSlot.RELIC, "Brass Compass", "Fear of the Unknown", FOTU_SOURCE))
        add(InvestigatorCardPreset("relic:crystal-ball", InvestigatorCardSlot.RELIC, "Crystal Ball", "Fear of the Unknown", FOTU_SOURCE))
        add(InvestigatorCardPreset("relic:jade-amulet", InvestigatorCardSlot.RELIC, "Jade Amulet", "Fear of the Unknown", FOTU_SOURCE))
        add(InvestigatorCardPreset("relic:lucky-coin", InvestigatorCardSlot.RELIC, "Lucky Coin", "Fear of the Unknown", FOTU_SOURCE))
        add(InvestigatorCardPreset("relic:replica-blade", InvestigatorCardSlot.RELIC, "Replica Blade", "Fear of the Unknown", FOTU_SOURCE, "7155578", listOf("Excalibur")))
        add(InvestigatorCardPreset("relic:ritual-mask", InvestigatorCardSlot.RELIC, "Ritual Mask", "Fear of the Unknown", FOTU_SOURCE))
        add(InvestigatorCardPreset("relic:ruby-talisman", InvestigatorCardSlot.RELIC, "Ruby Talisman", "Fear of the Unknown", FOTU_SOURCE))
        add(InvestigatorCardPreset("relic:runecarved-spear", InvestigatorCardSlot.RELIC, "Runecarved Spear", "Fear of the Unknown", FOTU_SOURCE))
        add(InvestigatorCardPreset("relic:shiny-jewel", InvestigatorCardSlot.RELIC, "Shiny Jewel", "Fear of the Unknown", FOTU_SOURCE, "7155578", listOf("Shining Trapezohedron")))
        add(InvestigatorCardPreset("relic:silver-key", InvestigatorCardSlot.RELIC, "Silver Key", "Fear of the Unknown", FOTU_SOURCE))
        add(InvestigatorCardPreset("relic:sinister-book", InvestigatorCardSlot.RELIC, "Sinister Book", "Fear of the Unknown", FOTU_SOURCE))
        add(InvestigatorCardPreset("relic:viol", InvestigatorCardSlot.RELIC, "Viol", "Season 4", S4_SOURCE, "7193296", listOf("Doomed Viol")))
        add(InvestigatorCardPreset("relic:wooden-pipe", InvestigatorCardSlot.RELIC, "Wooden Pipe", "Season 4", S4_SOURCE, "7193296", listOf("Transcendental Herbs")))
        add(InvestigatorCardPreset("relic:glowing-ring", InvestigatorCardSlot.RELIC, "Glowing Ring", "Season 4", S4_SOURCE, "7193296", listOf("Spectral Ring")))
        add(InvestigatorCardPreset("relic:old-rosary", InvestigatorCardSlot.RELIC, "Old Rosary", "Season 4", S4_SOURCE, "7193296", listOf("Blessed Chaplet")))
        add(InvestigatorCardPreset("relic:handheld-magnifier", InvestigatorCardSlot.RELIC, "Handheld Magnifier", "Season 4", S4_SOURCE, "7193296", listOf("Paranormal Detector")))
        add(InvestigatorCardPreset("relic:ritual-dagger", InvestigatorCardSlot.RELIC, "Ritual Dagger", "Season 4", S4_SOURCE, "7193296", listOf("Wicked Dagger")))
        add(InvestigatorCardPreset("relic:skull-bracelet", InvestigatorCardSlot.RELIC, "Skull Bracelet", "Season 4 bonus relics", BONUS_SOURCE, "7202381", listOf("Nefarious Skulls")))
        add(InvestigatorCardPreset("relic:ivory-figure", InvestigatorCardSlot.RELIC, "Ivory Figure", "Season 4 bonus relics", BONUS_SOURCE, "7202382", listOf("Eldritch Image")))
    }

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
    private val images = mapOf(
        "7155578" to "https://cf.geekdo-images.com/d9VRBQ9-nSO3vHXG0Box_Q__large/img/9c_azv-M26tF0_7sX0OUBJeA_VU=/fit-in/1024x1024/filters:no_upscale():strip_icc()/pic7155578.jpg",
        "7193296" to "https://cf.geekdo-images.com/7DqgLwCmz5nRgGdfnzWJ7w__large/img/T1BMeXNho_QBTxVd7p7woW9zL-Y=/fit-in/1024x1024/filters:no_upscale():strip_icc()/pic7193296.jpg",
        "7194664" to "https://cf.geekdo-images.com/LoihH95qISCN65Y4BPolYg__large/img/gWzRTsdoQUsHXtpFVHxNiBR_rUQ=/fit-in/1024x1024/filters:no_upscale():strip_icc()/pic7194664.jpg",
        "7194665" to "https://cf.geekdo-images.com/eXxdZPYIRlu5KWYN5aDotg__large/img/sZ4vzgQ3sqbgQJ8HgWynBazLeCM=/fit-in/1024x1024/filters:no_upscale():strip_icc()/pic7194665.jpg",
        "7194666" to "https://cf.geekdo-images.com/s9N_kiqkmJobPoJKgAMvcg__large/img/RcupJFfZEQhJVu6jBrMulVu3cUo=/fit-in/1024x1024/filters:no_upscale():strip_icc()/pic7194666.jpg",
        "7194667" to "https://cf.geekdo-images.com/2VYwm7ejaxW-_l0nxlEF-Q__large/img/ZNWqsRar_K24hv1_g1F5Lmbcquk=/fit-in/1024x1024/filters:no_upscale():strip_icc()/pic7194667.jpg",
        "7202381" to "https://cf.geekdo-images.com/RLfL4FsQhU5Nw9eFclRRyQ__large/img/cl-oUFqFVX7cV9knTOx0lCmldXU=/fit-in/1024x1024/filters:no_upscale():strip_icc()/pic7202381.jpg",
        "7202382" to "https://cf.geekdo-images.com/4ccHzw-Xr_62u4vU4DSDIg__large/img/E3YKnftvzc8luxZmPEmNStFWeSM=/fit-in/1024x1024/filters:no_upscale():strip_icc()/pic7202382.jpg",
    )
}
