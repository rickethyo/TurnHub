package com.turnhub.android.ui.tablet

import com.turnhub.android.protocol.GameProfile
import com.turnhub.android.protocol.TableState
import com.turnhub.android.standalone.LocalLibrary
import com.turnhub.android.standalone.StandaloneGame
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class PracticeTableTest {
    @Test fun `an empty lobby practices with two placeholder seats`() {
        val game = StandaloneGame().preview(paused = false)
        assertEquals(TableState.RUNNING, game.state)
        assertEquals(listOf("Player 1", "Player 2"), game.players.map { it.name })
    }

    @Test fun `a Star lobby fills to five seats and keeps the seated players first`() {
        val lobby = StandaloneGame(profile = GameProfile.MTG_STAR_COMMANDER).addPlayer("Ana")
        val game = lobby.preview()
        assertEquals(TableState.PAUSED, game.state)
        assertEquals(listOf("Ana", "Player 2", "Player 3", "Player 4", "Player 5"), game.players.map { it.name })
    }

    @Test fun `practice is playable and leaves the lobby untouched`() = runBlocking {
        val lobby = StandaloneGame().addPlayer("Ana").addPlayer("Ben")
        val practice = practiceTable(lobby, LocalLibrary())
        val seat = practice.state.value.game.toSummary(0, 0).players.first().seat()
        assertTrue(practice.life(seat, -3))
        assertEquals(lobby.startingLife - 3, practice.state.value.game.players.first().life)
        assertEquals(TableState.LOBBY, lobby.state)
    }
}
