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
    @Test fun `Death May Die practice fills only missing references and never changes real setup`() {
        val condition = com.turnhub.android.domain.InvestigatorCard("Chosen condition")
        val lobby = StandaloneGame(profile = GameProfile.DEATH_MAY_DIE).addPlayer("Ana").setUseRelics(true)
            .setInvestigatorCard(0, com.turnhub.android.domain.InvestigatorCardSlot.SANITY_CONDITION, condition)
        val game = lobby.preview(paused = false)
        assertEquals(TableState.RUNNING, game.state)
        assertEquals(2, game.players.size)
        assertEquals(condition, game.players[0].dashboard.sanityCondition)
        assertTrue(game.players.all { it.dashboard.sanityCondition != null && it.dashboard.relic != null })
        assertEquals(1, lobby.players.size)
        assertEquals(null, lobby.players[0].dashboard.relic)
        assertEquals(TableState.LOBBY, lobby.state)
    }
}
