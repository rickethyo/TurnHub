package com.turnhub.android.standalone

import com.turnhub.android.domain.InvestigatorCard
import com.turnhub.android.domain.InvestigatorCardCatalogue
import com.turnhub.android.domain.InvestigatorCardSlot
import com.turnhub.android.protocol.GameProfile
import org.junit.Assert.*
import org.junit.Test
import java.net.URI

class InvestigatorCardCatalogueTest {
    private val conditions = InvestigatorCardSlot.SANITY_CONDITION
    private val relics = InvestigatorCardSlot.RELIC
    private val bloodlust = InvestigatorCardCatalogue.find("condition:bloodlust")!!
    private val skull = InvestigatorCardCatalogue.find("relic:skull-bracelet")!!

    @Test fun `catalogue entries are bounded unique references and all images use the verified host`() {
        val entries = InvestigatorCardCatalogue.all
        assertEquals(entries.size, entries.map { it.id }.distinct().size)
        InvestigatorCardSlot.entries.forEach { slot ->
            val names = entries.filter { it.slot == slot }.map { it.name.lowercase() }
            assertEquals(names.size, names.distinct().size)
            assertTrue(names.isNotEmpty())
        }
        entries.forEach { entry ->
            assertEquals(entry.card(), entry.card().normalized())
            assertEquals("https", URI(entry.sourceUrl).scheme)
            entry.imageId?.let { assertEquals("cf.geekdo-images.com", URI(InvestigatorCardCatalogue.imageUrl(it)!!).host) }
            assertFalse(entry.card().rulesVerified)
            assertFalse(entry.card().transformedRulesVerified)
            assertTrue(entry.card().rules.length <= InvestigatorCard.MAX_RULES)
        }
    }

    @Test fun `search finds regular transformed and alternate condition names in only the requested slot`() {
        assertEquals(listOf("Skull Bracelet"), InvestigatorCardCatalogue.search(relics, "  nEFARIOUS  sKULLS ").map { it.name })
        assertEquals(listOf("Obsessive Disorder"), InvestigatorCardCatalogue.search(conditions, "OCD").map { it.name })
        assertEquals(listOf("R’lyehian"), InvestigatorCardCatalogue.search(conditions, "R'Lyehian").map { it.name })
        assertTrue(InvestigatorCardCatalogue.search(conditions, "Skull Bracelet").isEmpty())
        assertTrue(InvestigatorCardCatalogue.search(relics, "Bloodlust").isEmpty())
        assertTrue(InvestigatorCardCatalogue.search(relics, "Season 4").any { it.name == "Handheld Magnifier" })
        assertEquals(InvestigatorCardCatalogue.all.count { it.slot == relics }, InvestigatorCardCatalogue.search(relics, "").size)
    }

    @Test fun `renaming detaches the old reference while rules edits and whitespace retain it`() {
        val card = bloodlust.card().copy(rules = "My reminder")
        assertEquals(card, card.copy(name = " Bloodlust ").normalized())
        assertEquals(card.copy(name = "Homebrew", catalogueId = null), card.copy(name = "Homebrew").normalized())
        assertNull(card.copy(catalogueId = "https://untrusted.example/card").normalized()!!.catalogueId)
        val lobby = StandaloneGame().setFormat(GameProfile.DEATH_MAY_DIE).setUseRelics(true).addPlayer("A").addPlayer("B")
        assertSame(lobby, lobby.setInvestigatorCard(0, conditions, skull.card()))
        assertSame(lobby, lobby.setInvestigatorCard(0, relics, bloodlust.card()))
    }

    @Test fun `catalogue assignments save with editable notes and remain isolated between players`() {
        val condition = bloodlust.card().copy(rules = "Tokens: 2")
        val g = StandaloneGame().setFormat(GameProfile.DEATH_MAY_DIE).setUseRelics(true).addPlayer("A").addPlayer("B")
            .setInvestigatorCard(0, conditions, condition).setInvestigatorCard(0, relics, skull.card())
            .setInvestigatorCard(1, conditions, InvestigatorCard("My condition", "My rules"))
            .setInvestigatorCard(1, relics, InvestigatorCard("My relic"))
        val restored = StandaloneTable.gameFromJson(StandaloneTable.gameToJson(g))
        assertEquals(g.players, restored.players)
        assertEquals(condition, restored.toSummary(0, 0).players[0].dashboard.sanityCondition)
        val changed = restored.setInvestigatorCard(0, conditions, condition.copy(name = "Custom"))
        assertNull(changed.players[0].dashboard.sanityCondition!!.catalogueId)
        assertEquals("My condition", changed.players[1].dashboard.sanityCondition!!.name)
    }

    @Test fun `recovery drops unknown mismatched and wrong-slot references without losing player notes`() {
        val g = StandaloneGame().setFormat(GameProfile.DEATH_MAY_DIE).addPlayer("A").addPlayer("B")
            .setInvestigatorCard(0, conditions, bloodlust.card().copy(rules = "Keep these notes"))
        listOf("relic:skull-bracelet", "unknown", "https://untrusted.example").forEach { id ->
            val json = StandaloneTable.gameToJson(g)
            val card = json.getJSONArray("players").getJSONObject(0).getJSONObject("dashboard").getJSONObject("sanityCondition")
            card.put("catalogueId", id)
            if (id == skull.id) card.put("name", skull.name)
            val restored = StandaloneTable.gameFromJson(json).players[0].dashboard.sanityCondition!!
            assertNull(restored.catalogueId)
            assertEquals("Keep these notes", restored.rules)
        }
    }
}
