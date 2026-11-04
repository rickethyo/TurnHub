package com.turnhub.android.standalone

import com.turnhub.android.domain.*
import com.turnhub.android.protocol.GameProfile
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test

class InvestigatorRulesReviewTest {
    private val condition = InvestigatorCardCatalogue.find("condition:bloodlust")!!
    private val relic = InvestigatorCardCatalogue.find("relic:replica-blade")!!

    @Test fun `available reminders prefill both faces and never claim physical verification`() {
        val conditions = InvestigatorCardCatalogue.all.filter { it.slot == InvestigatorCardSlot.SANITY_CONDITION }
        val relics = InvestigatorCardCatalogue.all.filter { it.slot == InvestigatorCardSlot.RELIC }
        assertEquals(20, conditions.count { it.card().rules.isNotBlank() })
        assertEquals(10, relics.count { it.card().rules.isNotBlank() && it.card().transformedRules.isNotBlank() })
        assertTrue(relic.card().rules.contains("1 green die"))
        assertTrue(relic.card().transformedRules.contains("2 green dice"))
        (conditions + relics).forEach {
            assertFalse(it.card().rulesVerified)
            assertFalse(it.card().transformedRulesVerified)
            assertEquals(it.card(), it.card().normalized(it.slot))
        }
        assertTrue(InvestigatorCardCatalogue.find("relic:ancient-idol")!!.card().rules.isBlank())
    }

    @Test fun `editing a checked face invalidates only that face and renaming invalidates both`() {
        val checked = relic.card().verifyRules(true).verifyTransformedRules(true)
        assertTrue(checked.rulesVerified)
        assertTrue(checked.transformedRulesVerified)
        val frontEdit = checked.copy(rules = "A correction")
        assertFalse(frontEdit.rulesVerified)
        assertTrue(frontEdit.transformedRulesVerified)
        val backEdit = checked.copy(transformedRules = "A reverse correction")
        assertTrue(backEdit.rulesVerified)
        assertFalse(backEdit.transformedRulesVerified)
        assertFalse(checked.copy(name = "Other relic").rulesVerified)
        assertFalse(checked.copy(name = "Other relic").transformedRulesVerified)
        assertNull(frontEdit.normalized()!!.verifiedRulesHash)
        assertTrue(checked.copy(rules = " ${checked.rules} ").normalized()!!.rulesVerified)
        assertFalse(checked.verifyRules(false).rulesVerified)
        assertTrue(checked.verifyRules(false).transformedRulesVerified)
        assertFalse(InvestigatorCard("Empty").verifyRules(true).rulesVerified)
    }

    @Test fun `library saves physical checks independently of match assignments and keeps them for later games`() {
        val checked = relic.card().copy(rules = "My corrected starting face and flip requirement").verifyRules(true).verifyTransformedRules(true)
        val library = LocalLibrary().withInvestigatorReference(relic.id, checked)
        val restored = LocalLibrary.fromJson(library.toJson())
        assertEquals(library, restored)
        assertTrue(restored.investigatorReferences.getValue(relic.id).rulesVerified)
        assertTrue(restored.investigatorReferences.getValue(relic.id).transformedRulesVerified)
        val old = relic.card().copy(rules = "Existing match notes")
        val game = StandaloneGame().setFormat(GameProfile.DEATH_MAY_DIE).setUseRelics(true).addPlayer("A").addPlayer("B")
            .setInvestigatorCard(0, relic.slot, old)
        val store = MemoryStandaloneStore(game, restored)
        val table = StandaloneTable(store)
        assertTrue(table.saveInvestigatorReference(relic.id, checked.copy(rules = "Next correction")))
        assertEquals(old, table.state.value.game.players[0].dashboard.relic)
        table.clearPlayers()
        assertTrue(table.state.value.library.investigatorReferences.containsKey(relic.id))
        val recovered = StandaloneTable(store)
        assertEquals("Next correction", recovered.state.value.library.investigatorReferences.getValue(relic.id).rules)
        assertFalse(recovered.state.value.library.investigatorReferences.getValue(relic.id).rulesVerified)
    }

    @Test fun `legacy typed notes stay verbatim and missing checks default to unverified`() {
        val legacy = JSONObject().put("name", condition.name).put("catalogueId", condition.id).put("rules", "My personal reminder")
        val loaded = InvestigatorCard.fromJson(legacy, condition.slot)!!
        assertEquals("My personal reminder", loaded.rules)
        assertTrue(loaded.transformedRules.isEmpty())
        assertFalse(loaded.rulesVerified)
        val library = LocalLibrary().toJson().apply { remove("investigatorReferences") }
        assertTrue(LocalLibrary.fromJson(library).investigatorReferences.isEmpty())
        val game = StandaloneGame().setFormat(GameProfile.DEATH_MAY_DIE).setUseRelics(true).addPlayer("A").addPlayer("B")
            .setInvestigatorCard(0, condition.slot, loaded.verifyRules(true))
            .setInvestigatorCard(0, relic.slot, relic.card().verifyRules(true).verifyTransformedRules(true))
        val recovered = StandaloneTable.gameFromJson(StandaloneTable.gameToJson(game))
        assertEquals(game.players, recovered.players)
    }

    @Test fun `malformed oversized and wrong identity references are rejected without losing valid notes`() {
        val library = LocalLibrary()
        assertSame(library, library.withInvestigatorReference("unknown", condition.card()))
        assertSame(library, library.withInvestigatorReference(condition.id, relic.card()))
        assertSame(library, library.withInvestigatorReference(condition.id, condition.card().copy(rules = "x".repeat(2_001))))
        val badCheck = relic.card().toJson().put("verifiedRulesHash", "fake").put("verifiedTransformedRulesHash", "fake")
        val recovered = InvestigatorCard.fromJson(badCheck, relic.slot)!!
        assertFalse(recovered.rulesVerified)
        assertFalse(recovered.transformedRulesVerified)
        assertNull(recovered.verifiedRulesHash)
        val conditionWithBack = condition.card().copy(transformedRules = "Wrong face").verifyTransformedRules(true).normalized(condition.slot)!!
        assertTrue(conditionWithBack.transformedRules.isEmpty())
        assertNull(conditionWithBack.verifiedTransformedRulesHash)
        val json = library.toJson().put("investigatorReferences", JSONObject().put("unknown", badCheck)
            .put(condition.id, relic.card().toJson()).put(relic.id, badCheck))
        assertEquals(setOf(relic.id), LocalLibrary.fromJson(json).investigatorReferences.keys)
    }

    @Test fun `failed persistence leaves reference and match snapshots unchanged`() {
        val initial = StandaloneGame().setFormat(GameProfile.DEATH_MAY_DIE).addPlayer("A").addPlayer("B")
        val memory = MemoryStandaloneStore(initial, LocalLibrary())
        val failing = object : StandaloneStore by memory {
            override fun saveSnapshot(game: String, library: String) = false
        }
        val table = StandaloneTable(failing)
        assertFalse(table.saveInvestigatorReference(condition.id, condition.card().verifyRules(true)))
        assertTrue(table.state.value.library.investigatorReferences.isEmpty())
        assertEquals(initial, table.state.value.game)
        assertFalse(table.saveInvestigatorReference("unknown", condition.card()))
    }
}
