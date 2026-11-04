package com.turnhub.android.domain

/**
 * Empty stand-in for Death May Die's catalogue. CMON's licensing keeps the real
 * content (investigators, cards, art links, story) off GitHub: it lives only on
 * the owner's PC as src/dmd/java/.../DmdContent.generated.kt (gitignored), which
 * build.gradle.kts compiles instead of this file when present. Without it the
 * cooperative mode still builds, with empty pickers and catalogues.
 */
internal object DmdContent {
    val boxes: List<InvestigatorBox> = emptyList()
    val investigatorArt: Map<String, String> = emptyMap()
    val commonSkills: Map<String, List<String>> = emptyMap()
    val knownSkills: Map<String, List<InvestigatorSkill>> = emptyMap()
    val skillPresets: List<InvestigatorSkill> = emptyList()
    val skillFilters: List<String> = emptyList()
    val cards: List<InvestigatorCardPreset> = emptyList()
    val cardImages: Map<String, String> = emptyMap()
    val cardDrafts: Map<String, InvestigatorRulesDraft> = emptyMap()
    val elderOnes: List<StoryGroup> = emptyList()
    val episodes: List<StoryGroup> = emptyList()
}
