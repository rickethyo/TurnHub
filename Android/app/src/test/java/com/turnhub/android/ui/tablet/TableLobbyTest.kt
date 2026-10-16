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

    @Test fun `standard magic doesn't offer commander-only modes`() {
        assertEquals(listOf(GameMode.STANDARD, GameMode.TWO_HEADED_GIANT), GameMode.availableFor(GameProfile.MTG))
        assertEquals(GameMode.entries, GameMode.availableFor(GameProfile.MTG_COMMANDER))
        assertEquals(GameMode.entries, GameMode.availableFor(GameProfile.MTG_STAR_COMMANDER))
        assertEquals("Standard", FORMATS.single { it.profile == GameProfile.MTG }.label)
    }
}
