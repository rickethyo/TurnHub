package com.turnhub.android.domain

/** References for cards assigned to an investigator in this match. */
enum class InvestigatorCardSlot(val label: String) {
    SANITY_CONDITION("Sanity condition"),
    RELIC("Relic"),
}

data class InvestigatorCard(
    val name: String,
    val rules: String = "",
    val catalogueId: String? = null,
    val transformedRules: String = "",
    val verifiedRulesHash: String? = null,
    val verifiedTransformedRulesHash: String? = null,
) {
    val rulesVerified: Boolean get() = rules.isNotBlank() && verifiedRulesHash == fingerprint(rules)
    val transformedRulesVerified: Boolean get() = transformedRules.isNotBlank() && verifiedTransformedRulesHash == fingerprint(transformedRules)

    fun verifyRules(checked: Boolean): InvestigatorCard = copy(verifiedRulesHash = if (checked && rules.isNotBlank()) fingerprint(rules) else null)
    fun verifyTransformedRules(checked: Boolean): InvestigatorCard = copy(
        verifiedTransformedRulesHash = if (checked && transformedRules.isNotBlank()) fingerprint(transformedRules) else null)

    private fun fingerprint(text: String): String = java.security.MessageDigest.getInstance("SHA-256")
        .digest((name.trim().lowercase(java.util.Locale.ROOT) + "\u0000" + text.trim()).toByteArray(Charsets.UTF_8))
        .joinToString("") { "%02x".format(it.toInt() and 255) }

    fun normalized(slot: InvestigatorCardSlot? = null): InvestigatorCard? {
        val entry = InvestigatorCardCatalogue.find(catalogueId)
        val relic = (slot ?: entry?.slot) != InvestigatorCardSlot.SANITY_CONDITION
        val clean = copy(name = name.trim(), rules = rules.trim(), transformedRules = transformedRules.trim().takeIf { relic }.orEmpty(),
            catalogueId = entry?.id?.takeIf { entry.name.equals(name.trim(), ignoreCase = true) },
            verifiedRulesHash = verifiedRulesHash.takeIf { rulesVerified },
            verifiedTransformedRulesHash = verifiedTransformedRulesHash.takeIf { relic && transformedRulesVerified })
        return clean.takeIf { it.name.isNotEmpty() && it.name.length <= MAX_NAME && it.rules.length <= MAX_RULES && it.transformedRules.length <= MAX_RULES }
    }

    fun toJson(): org.json.JSONObject = org.json.JSONObject().put("name", name).put("rules", rules)
        .put("catalogueId", catalogueId ?: org.json.JSONObject.NULL).put("transformedRules", transformedRules)
        .put("verifiedRulesHash", verifiedRulesHash ?: org.json.JSONObject.NULL)
        .put("verifiedTransformedRulesHash", verifiedTransformedRulesHash ?: org.json.JSONObject.NULL)

    companion object {
        fun fromJson(json: org.json.JSONObject, slot: InvestigatorCardSlot): InvestigatorCard? {
            val name = json.opt("name") as? String ?: return null
            val rules = if (json.has("rules")) json.opt("rules") as? String ?: return null else ""
            val transformed = if (json.has("transformedRules")) json.opt("transformedRules") as? String ?: return null else ""
            return InvestigatorCard(name, rules, json.opt("catalogueId") as? String, transformed,
                json.opt("verifiedRulesHash") as? String, json.opt("verifiedTransformedRulesHash") as? String).normalized(slot)
        }

        const val MAX_NAME = 80
        const val MAX_RULES = 2_000
    }
}
