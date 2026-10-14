package com.turnhub.android.standalone

import com.turnhub.android.protocol.GameProfile
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class StatsSharingTest {
    private fun player(name: String, id: String, life: Int, out: Int? = null, conceded: Boolean = false) =
        GameRecord.Player(name, null, life, 5, outOrder = out, commanderDamageReceived = 0, localId = id, conceded = conceded)

    private fun game(id: String, winner: Int?, vararg players: GameRecord.Player, teams: Boolean = false) =
        GameRecord(id, GameProfile.MTG_COMMANDER, 40, 0, 125_000, 0, winner, players.toList(), twoHeadedGiant = teams)

    private val library = LocalLibrary(
        players = listOf(SavedLocalPlayer("a", "Ana"), SavedLocalPlayer("b", "Bo"), SavedLocalPlayer("c", "Cy"), SavedLocalPlayer("d", "Dee")),
        history = listOf(
            game("1", 1, player("Ana", "a", 0, out = 1), player("Bo", "b", 12), player("Cy", "c", 0, out = 2, conceded = true)),
            game("2", 0, player("Ana", "a", 3), player("Bo", "b", 0, out = 1)),
            game("3", 1, player("Ana", "a", 0, out = 1), player("Bo", "b", 7)),
        ),
    )

    @Test fun `leaderboard ranks most wins first and leaves out players with no games`() {
        val board = library.leaderboard()
        assertEquals(listOf("Bo", "Ana", "Cy"), board.map { it.player.name })
        assertEquals(66, board[0].winPercent)
        assertEquals(listOf(2, 1, 0), board.map { it.stats.totals.won })
    }

    @Test fun `stats text carries the totals without any account details`() {
        val text = library.statsShareText("b")
        assertTrue(text, text.startsWith("Bo's TurnHub stats\n3 games · 2 wins (66%)"))
        assertTrue(!text.contains(library.players[1].profileId))
    }

    @Test fun `game summary names the winner and places everyone`() {
        val text = library.history[0].shareSummary("Commander")
        assertEquals("""
            🏆 Bo won!
            Commander · 3 players · 2:05
            Ana: 0 life · 5 turns · 3rd
            Bo: 12 life · 5 turns · winner
            Cy: 0 life · 5 turns · 2nd (conceded)
            Played with TurnHub
        """.trimIndent(), text)
    }

    @Test fun `draws and Two-Headed Giant teams read naturally`() {
        assertTrue(game("x", null, player("Ana", "a", 5), player("Bo", "b", 5)).shareSummary("Magic").startsWith("Game over: a draw!"))
        val teams = game("y", 0, player("Ana", "a", 9), player("Bo", "b", 9), player("Cy", "c", 0, out = 1), player("Dee", "d", 0, out = 1), teams = true)
        assertTrue(teams.shareSummary("Magic").startsWith("🏆 Ana and Bo won!\nMagic · Two-Headed Giant"))
        assertEquals(listOf("1st", "2nd", "3rd", "4th", "11th", "12th", "13th", "21st", "22nd"),
            listOf(1, 2, 3, 4, 11, 12, 13, 21, 22).map(::ordinal))
    }
}
