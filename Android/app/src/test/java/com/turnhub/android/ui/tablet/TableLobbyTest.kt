package com.turnhub.android.ui.tablet

import com.turnhub.android.protocol.GameProfile
import org.junit.Assert.assertEquals
import org.junit.Test

class TableLobbyTest {
    @Test fun `both authorities receive matching mode settings including format transitions`() {
        assertEquals(LobbyModeSettings(null, 60, true), settingsForMode(GameProfile.MTG_COMMANDER, GameMode.TWO_HEADED_GIANT))
        assertEquals(LobbyModeSettings(null, 30, true), settingsForMode(GameProfile.MTG, GameMode.TWO_HEADED_GIANT))
        assertEquals(LobbyModeSettings(GameProfile.MTG_STAR_COMMANDER, 40, false), settingsForMode(GameProfile.MTG, GameMode.STAR_COMMANDER))
        assertEquals(LobbyModeSettings(GameProfile.MTG_COMMANDER, 60, true), settingsForMode(GameProfile.MTG_STAR_COMMANDER, GameMode.TWO_HEADED_GIANT))
        assertEquals(LobbyModeSettings(GameProfile.MTG_COMMANDER, 40, false), settingsForMode(GameProfile.MTG_STAR_COMMANDER, GameMode.STANDARD))
    }

    @Test fun `bag mode stays under commander and returns to regular commander`() {
        assertEquals(LobbyModeSettings(GameProfile.MTG_BAG_OF_DOOM, 40, false), settingsForMode(GameProfile.MTG_COMMANDER, GameMode.BAG_OF_DOOM))
        assertEquals(LobbyModeSettings(GameProfile.MTG_COMMANDER, 40, false), settingsForMode(GameProfile.MTG_BAG_OF_DOOM, GameMode.STANDARD))
        assertEquals(GameMode.BAG_OF_DOOM, GameMode.of(GameProfile.MTG_BAG_OF_DOOM, false))
        assertEquals(true, formatSelected(GameProfile.MTG_BAG_OF_DOOM, GameProfile.MTG_COMMANDER))
    }

    @Test fun `emperor follows the deck format and keeps its seating between magic and commander`() {
        assertEquals(LobbyModeSettings(GameProfile.MTG_EMPEROR, 20, false), settingsForMode(GameProfile.MTG, GameMode.EMPEROR))
        assertEquals(LobbyModeSettings(GameProfile.MTG_COMMANDER_EMPEROR, 40, false), settingsForMode(GameProfile.MTG_COMMANDER, GameMode.EMPEROR))
        assertEquals(LobbyModeSettings(GameProfile.MTG, 20, false), settingsForMode(GameProfile.MTG_EMPEROR, GameMode.STANDARD))
        assertEquals(LobbyModeSettings(GameProfile.MTG_COMMANDER, 60, true), settingsForMode(GameProfile.MTG_COMMANDER_EMPEROR, GameMode.TWO_HEADED_GIANT))
        assertEquals(GameMode.EMPEROR, GameMode.of(GameProfile.MTG_COMMANDER_EMPEROR, false))
        assertEquals(true, formatSelected(GameProfile.MTG_EMPEROR, GameProfile.MTG))
        assertEquals(false, formatSelected(GameProfile.MTG_EMPEROR, GameProfile.MTG_COMMANDER))
        assertEquals(GameProfile.MTG_COMMANDER_EMPEROR, emperorFormat(GameProfile.MTG_EMPEROR, GameProfile.MTG_COMMANDER))
        assertEquals(GameProfile.MTG_EMPEROR, emperorFormat(GameProfile.MTG_COMMANDER_EMPEROR, GameProfile.MTG))
        assertEquals(GameProfile.YUGIOH, emperorFormat(GameProfile.MTG_EMPEROR, GameProfile.YUGIOH))
        assertEquals(GameProfile.MTG_COMMANDER, emperorFormat(GameProfile.MTG, GameProfile.MTG_COMMANDER))
    }

    @Test fun `standard magic doesn't offer commander-only modes`() {
        assertEquals(listOf(GameMode.STANDARD, GameMode.TWO_HEADED_GIANT, GameMode.EMPEROR), GameMode.availableFor(GameProfile.MTG))
        assertEquals(GameMode.entries, GameMode.availableFor(GameProfile.MTG_COMMANDER))
        assertEquals(GameMode.entries, GameMode.availableFor(GameProfile.MTG_STAR_COMMANDER))
        assertEquals("Magic · 20 life", FORMATS.single { it.profile == GameProfile.MTG }.label)
    }

    @Test fun `tile cards show in every game but are named as commanders only in commander`() {
        val lobby = com.turnhub.android.standalone.StandaloneGame().setFormat(GameProfile.MTG_COMMANDER)
            .addPlayer("A").addPlayer("B").addPlayer("C").addPlayer("D").setCommander(0, "Atraxa")
        assertEquals("Atraxa", lobby.toSummary(0, 0).players[0].namedCommander)
        val teams = lobby.setFormat(GameProfile.MTG).setTwoHeadedGiant(true).toSummary(0, 0).players[0]
        assertEquals("Atraxa", teams.commander)
        assertEquals(null, teams.namedCommander)
        // Tile art isn't tied to the game: a Magic card can sit behind a Yu-Gi-Oh! player.
        val duel = lobby.setFormat(GameProfile.YUGIOH).toSummary(0, 0).players[0]
        assertEquals("Atraxa", duel.commander)
        assertEquals(null, duel.namedCommander)
        // Artwork from another game never replaces or suppresses the named commander.
        val pokemonArt = lobby.setCommander(2, "Atraxa", "pokemon:base1-58").toSummary(0, 0).players[2]
        assertEquals("pokemon:base1-58", pokemonArt.commanderArt)
        assertEquals("Atraxa", pokemonArt.namedCommander)
        val decorative = lobby.setCommander(2, "Pikachu", "pokemon:base1-58", cardOnly = true).toSummary(0, 0).players[2]
        assertEquals(null, decorative.namedCommander)
        assertEquals(null, lobby.setCommander(2, "Pikachu", "pokemon:../x").players[2].commanderArt)

        // A random card in Commander is only tile art, and survives saving as such.
        val random = lobby.setCommander(1, "Llanowar Elves", cardOnly = true)
        val restored = com.turnhub.android.standalone.StandaloneTable.gameFromJson(
            com.turnhub.android.standalone.StandaloneTable.gameToJson(random)).toSummary(0, 0).players[1]
        assertEquals("Llanowar Elves", restored.commander)
        assertEquals(null, restored.namedCommander)
    }
}
