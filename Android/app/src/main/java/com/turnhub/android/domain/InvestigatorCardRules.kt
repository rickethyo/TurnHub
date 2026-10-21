package com.turnhub.android.domain

/** Original mechanical reminders, pending owner comparison with physical cards.
 * These are not exact card transcriptions. Missing or uncertain effects stay empty.
 */
data class InvestigatorRulesDraft(val rules: String, val transformedRules: String = "", val source: String,
    val reviewNote: String = "Compare every effect with your printed card, including its flip requirement.")

object InvestigatorCardRules {
    private val drafts = DmdContent.cardDrafts
    fun draft(id: String?): InvestigatorRulesDraft? = drafts[id]
}
