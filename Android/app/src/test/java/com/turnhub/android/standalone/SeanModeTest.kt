package com.turnhub.android.standalone

import com.turnhub.android.domain.StarCommander
import com.turnhub.android.protocol.GameProfile
import com.turnhub.android.protocol.TableState
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertSame
import org.junit.Assert.assertTrue
import org.junit.Test

class SeanModeTest {
    private val names = listOf("Michael", "Nex", "Ricky", "Micky", "Sean")

    private fun finishedStar(): StandaloneGame {
        var game = StandaloneGame(profile = GameProfile.MTG_STAR_COMMANDER)
        names.forEach { game = game.addPlayer(it) }
        game = game.setCommander(0, "The Serpent Society").setCommander(4, "  Choco, Seeker of Paradise ")
        return game.start(0, "g1").endWithoutWinner(1_000)
    }

    private fun allies(game: StandaloneGame, name: String): Set<String> {
        val seat = game.players.indexOfFirst { it.name == name }
        return game.players.indices.filter { it != seat && StarCommander.allied(seat, it) }.map { game.players[it].name }.toSet()
    }

    @Test fun `Sean Mode turns every pair of allies into enemies`() {
        val before = finishedStar()
        val after = before.seanModeRematch(2_000, "g2")
        assertEquals(TableState.RUNNING, after.state)
        assertEquals("g2", after.gameId)
        names.forEach { name ->
            val old = allies(before, name)
            val new = allies(after, name)
            assertTrue("$name keeps no ally", old.intersect(new).isEmpty())
            assertEquals(2, new.size)
        }
    }

    @Test fun `commanders ride along through starts, rematches and Sean Mode`() {
        val before = finishedStar()
        assertEquals("Choco, Seeker of Paradise", before.players[4].commander)
        val sean = before.seanModeRematch(2_000, "g2")
        assertEquals("The Serpent Society", sean.players.first { it.name == "Michael" }.commander)
        assertEquals("Choco, Seeker of Paradise", sean.toSummary(0, 0).players.first { it.label == "Sean" }.commander)
        assertEquals("The Serpent Society", before.rematch(2_000, "g3").players[0].commander)
        assertNull(before.setCommander(0, "  ").players[0].commander)
    }

    @Test fun `Sean Mode needs a finished five-player Star game`() {
        val running = finishedStar().rematch(2_000, "g2")
        assertSame(running, running.seanModeRematch(3_000, "g3"))
        val commander = StandaloneGame().addPlayer("A").addPlayer("B").start(0, "c").endWithoutWinner(1)
        assertSame(commander, commander.seanModeRematch(2, "d"))
    }

    @Test fun `commander names survive saving`() {
        val game = finishedStar()
        val restored = StandaloneTable.gameFromJson(StandaloneTable.gameToJson(game))
        assertEquals(game.players.map { it.commander }, restored.players.map { it.commander })
    }
}
