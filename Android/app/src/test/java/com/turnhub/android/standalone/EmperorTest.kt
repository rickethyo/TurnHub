package com.turnhub.android.standalone

import com.turnhub.android.domain.Emperor
import com.turnhub.android.protocol.GameProfile
import com.turnhub.android.protocol.TableState
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertSame
import org.junit.Assert.assertTrue
import org.junit.Test
import kotlin.random.Random

class EmperorTest {
    /** Always picks the first choice: team A's emperor (seat 1) goes first. */
    private val first = object : Random() {
        override fun nextBits(bitCount: Int): Int = 0
    }

    private val names = listOf("Ana", "Ben", "Cy", "Dee", "Eve", "Fay")

    private fun lobby(profile: GameProfile = GameProfile.MTG_EMPEROR): StandaloneGame =
        names.fold(StandaloneGame().setFormat(profile)) { game, name -> game.addPlayer(name, localId = name) }

    private fun game(profile: GameProfile = GameProfile.MTG_EMPEROR) = lobby(profile).start(1_000, "e1", first)

    private val none = List(6) { false }

    private fun out(vararg seats: Int) = List(6) { it in seats }

    @Test
    fun `seats 1 to 3 and 4 to 6 are the teams with each emperor in the middle`() {
        assertEquals(listOf(0, 0, 0, 1, 1, 1), (0 until 6).map(Emperor::team))
        assertEquals(listOf(1, 4), (0 until 6).filter(Emperor::isEmperor))
        assertEquals(listOf(1, 2, 1, 1, 2, 1), (0 until 6).map(Emperor::range))
    }

    @Test
    fun `at the start generals reach their emperor and the facing general and emperors reach all four generals`() {
        // CR 809.3a: emperors 2 seats, generals 1.
        assertEquals(listOf(1, 5), Emperor.inRange(0, none))
        assertEquals(setOf(0, 2, 3, 5), Emperor.inRange(1, none).toSet())
        assertEquals(listOf(1, 3), Emperor.inRange(2, none))
        assertTrue((0 until 6).none { 4 in Emperor.inRange(it, none) && Emperor.team(it) == 0 })
        // CR 809.3c: only the facing generals can attack; the emperors can't attack anyone.
        assertEquals(listOf(5), Emperor.attackable(0, none))
        assertEquals(emptyList<Int>(), Emperor.attackable(1, none))
        assertEquals(listOf(3), Emperor.attackable(2, none))
        assertEquals(emptyList<Int>(), Emperor.attackable(4, none))
    }

    @Test
    fun `when a general falls the gap closes and the rival general reaches the emperor`() {
        // Team A's general at seat 0 is out: Team B's general at seat 5 now sits next to emperor A.
        val gone = out(0)
        assertEquals(listOf(1), Emperor.attackable(5, gone))
        assertEquals(listOf(5), Emperor.attackable(1, gone))
        assertTrue(4 in Emperor.inRange(1, gone))
        assertEquals(emptyList<Int>(), Emperor.inRange(0, gone))
        assertNull(Emperor.winners(gone))
    }

    @Test
    fun `emperor needs exactly six players, starts at 20 or 40 and never has two-headed giant`() {
        assertTrue(!lobby().removePlayer(5).canStart)
        assertTrue(!lobby().addPlayer("Gus").canStart)
        assertTrue(lobby().canStart)
        assertEquals(List(6) { 20 }, game().players.map { it.life })
        assertEquals(List(6) { 40 }, game(GameProfile.MTG_COMMANDER_EMPEROR).players.map { it.life })
        val seated = lobby()
        assertSame(seated, seated.setTwoHeadedGiant(true))
        val fromTeams = StandaloneGame().setFormat(GameProfile.MTG).setTwoHeadedGiant(true).setFormat(GameProfile.MTG_EMPEROR)
        assertTrue(!fromTeams.twoHeadedGiant)
        assertEquals(6, officialRules(GameProfile.MTG_EMPEROR, false)?.players)
        assertNull(officialRules(GameProfile.MTG_COMMANDER_EMPEROR, false))
        assertTrue(!GameProfile.MTG_EMPEROR.atlasSupported && !GameProfile.MTG_COMMANDER_EMPEROR.atlasSupported)
    }

    @Test
    fun `a random emperor goes first`() {
        assertEquals(1, game().active)
        val last = object : Random() { override fun nextBits(bitCount: Int): Int = 1 }
        assertEquals(4, lobby().start(1_000, "e2", last).active)
    }

    @Test
    fun `a fallen general keeps the game going and the team wins when the rival emperor is out`() {
        var game = game().changeLife(0, -20)
        assertTrue(game.players[0].eliminated)
        assertEquals(TableState.RUNNING, game.state)
        // Five players still in, but the game ends the moment emperor B is out.
        game = game.eliminate(4, 5_000)
        assertEquals(TableState.GAME_OVER, game.state)
        assertEquals(listOf(0, 1, 2), game.winners)
        assertEquals(0, game.winner)
        val summary = game.toSummary(6_000, 0)
        assertTrue(summary.isWinner(1) && summary.isWinner(3) && !summary.isWinner(4))
    }

    @Test
    fun `both emperors out together is a draw and a claimed win is the team's`() {
        assertEquals(emptyList<Int>(), Emperor.winners(out(1, 4)))
        assertEquals(listOf(0, 1, 2), Emperor.winners(out(4)))
        val claimed = game().claimWin(3, 2_000)
        assertEquals(listOf(3, 4, 5), claimed.winners)
    }

    @Test
    fun `teammates deal no commander damage and the summary shows each player's range`() {
        val game = game(GameProfile.MTG_COMMANDER_EMPEROR)
        assertSame(game, game.commanderDamage(1, source = 0, commander = 1, delta = 5))
        assertEquals(35, game.commanderDamage(0, source = 5, commander = 1, delta = 5).players[0].life)
        val summary = game.toSummary(1_000, 0)
        assertTrue(!summary.commanderDamageAllowed(2, 1) && summary.commanderDamageAllowed(1, 6))
        assertEquals(listOf("Fay"), summary.emperorTargets(1).map { it.label })
        assertEquals(listOf("Ben", "Fay"), summary.emperorReach(1).map { it.label })
        assertEquals(emptyList<String>(), summary.emperorTargets(2).map { it.label })
        // The ranges close up once a general is out.
        val closed = game.eliminate(0, 2_000).toSummary(3_000, 0)
        assertEquals(listOf("Fay"), closed.emperorTargets(2).map { it.label })
        assertEquals(emptyList<String>(), closed.emperorReach(1).map { it.label })
    }
}
