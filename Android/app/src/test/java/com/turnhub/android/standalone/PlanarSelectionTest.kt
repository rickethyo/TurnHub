package com.turnhub.android.standalone

import com.turnhub.android.protocol.GameProfile
import com.turnhub.android.protocol.TableState
import org.junit.Assert.*
import org.junit.Test
import kotlin.random.Random

class PlanarSelectionTest {
    private val cards = (1..12).map { PlanarCard("p$it") } + (1..10).map { PlanarCard("f$it", true) }
    private val choice = PlanarSelection("My collection", (1..10).map { "p$it" }.toSet() + (1..9).map { "f$it" })
    private fun lobby() = StandaloneGame().setFormat(GameProfile.MTG_COMMANDER).addPlayer("A").addPlayer("B").setPlanechase(true)
    private fun allCards(game: StandaloneGame) = game.planechase.deck + game.planechase.faceUp

    @Test fun `chosen pool persists across start reload reset and rematch with phenomenon cap`() {
        val store = MemoryStandaloneStore(lobby(), LocalLibrary())
        val table = StandaloneTable(store, planarPool = { cards }, planarRandom = Random(1))
        assertTrue(table.savePlanarDeck(choice))
        assertEquals(listOf(choice), StandaloneTable(store).state.value.library.planarDecks)
        table.start()
        val game = table.state.value.game
        assertEquals(choice, game.planechase.selection)
        assertEquals(10, allCards(game).count { !it.phenomenon })
        assertEquals(4, allCards(game).count { it.phenomenon })
        assertTrue(allCards(game).all { it.id in choice.cardIds!! })
        val reloaded = StandaloneTable(store, planarPool = { cards })
        assertEquals(game, reloaded.state.value.game)
        assertFalse(reloaded.setPlanarSelection(PlanarSelection()))
        assertFalse(reloaded.savePlanarDeck(PlanarSelection("Changed", setOf("p12"))))
        assertFalse(reloaded.deletePlanarDeck(choice.name))
        assertEquals(game, reloaded.state.value.game)
        val finished = game.endWithoutWinner(100000)
        val again = finished.rematch(100001, "next", Random(1), Planechase.shuffledDeck(choice.pool(cards), 2, true, Random(3)))
        assertEquals(choice, again.planechase.selection)
        assertTrue(allCards(again).all { it.id in choice.cardIds!! })
        assertEquals(choice, again.reset().planechase.selection)
    }

    @Test fun `empty phenomenon only missing and oversized choices are refused before saving or starting`() {
        val store = MemoryStandaloneStore(lobby(), LocalLibrary())
        val table = StandaloneTable(store, planarPool = { cards })
        listOf(PlanarSelection("Empty", emptySet()), PlanarSelection("Phenomena", setOf("f1")),
            PlanarSelection("Missing", setOf("p1", "missing")), PlanarSelection("x".repeat(65), setOf("p1"))).forEach {
            assertFalse(table.savePlanarDeck(it))
        }
        assertTrue(table.savePlanarDeck(choice))
        val unavailable = StandaloneTable(store, planarPool = { listOf(cards.first()) })
        unavailable.start()
        assertEquals(TableState.LOBBY, unavailable.state.value.game.state)
        assertTrue(unavailable.setPlanarSelection(PlanarSelection()))
        unavailable.start()
        assertEquals(TableState.RUNNING, unavailable.state.value.game.state)
    }

    @Test fun `presets can be replaced or deleted without changing the copied choice`() {
        val store = MemoryStandaloneStore(lobby(), LocalLibrary())
        val table = StandaloneTable(store, planarPool = { cards })
        assertTrue(table.savePlanarDeck(choice))
        val replacement = choice.copy(cardIds = setOf("p12"))
        assertTrue(table.savePlanarDeck(replacement))
        assertEquals(listOf(replacement), table.state.value.library.planarDecks)
        assertTrue(table.deletePlanarDeck(choice.name))
        val reload = StandaloneTable(store, planarPool = { cards })
        assertTrue(reload.state.value.library.planarDecks.isEmpty())
        assertEquals(replacement, reload.state.value.game.planechase.selection)
        reload.start()
        assertEquals(listOf("p12"), allCards(reload.state.value.game).map { it.id })
    }

    @Test fun `late deal uses the selected pool and old snapshots default to all cards`() {
        val game = lobby().setPlanarSelection(choice).start(0, "late", Random(1), emptyList())
        val store = MemoryStandaloneStore(game, LocalLibrary())
        val table = StandaloneTable(store, planarPool = { cards })
        table.dealPlanarDeck()
        assertTrue(allCards(table.state.value.game).all { it.id in choice.cardIds!! })
        assertEquals(14, allCards(table.state.value.game).size)
        val old = game.planechase.toJson().apply { remove("selection") }
        assertEquals(PlanarSelection(), Planechase.fromJson(old).selection)
        val library = LocalLibrary().toJson().apply { remove("planarDecks") }
        assertTrue(LocalLibrary.fromJson(library).planarDecks.isEmpty())
    }

    @Test fun `malformed card selection is not silently treated as all cards`() {
        val malformed = choice.toJson().put("cardIds", "invalid")
        assertTrue(runCatching { PlanarSelection.fromJson(malformed) }.isFailure)
    }

    @Test fun `failed persistence does not publish a choice or preset`() {
        val backing = MemoryStandaloneStore(lobby(), LocalLibrary())
        val failing = object : StandaloneStore by backing {
            override fun saveSnapshot(game: String, library: String) = false
        }
        val table = StandaloneTable(failing, planarPool = { cards })
        assertFalse(table.savePlanarDeck(choice))
        assertTrue(table.state.value.library.planarDecks.isEmpty())
        assertEquals(PlanarSelection(), table.state.value.game.planechase.selection)
        assertNotNull(table.state.value.storageProblem)
        assertEquals(PlanarSelection(), StandaloneTable(backing).state.value.game.planechase.selection)
    }

    @Test fun `preset limit allows replacing an existing name and disabled phenomena stay out`() {
        val table = StandaloneTable(MemoryStandaloneStore(lobby(), LocalLibrary()), planarPool = { cards })
        repeat(20) { assertTrue(table.savePlanarDeck(choice.copy(name = "Deck $it"))) }
        assertFalse(table.savePlanarDeck(choice.copy(name = "Twenty first")))
        assertTrue(table.savePlanarDeck(choice.copy(name = "Deck 0")))
        table.setPlanarPhenomena(false)
        table.start()
        assertTrue(allCards(table.state.value.game).none { it.phenomenon })
        assertEquals(10, allCards(table.state.value.game).size)
    }
}
