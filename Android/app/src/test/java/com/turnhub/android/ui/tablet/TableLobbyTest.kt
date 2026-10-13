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
}
