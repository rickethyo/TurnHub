package com.turnhub.android.ui.tablet

import com.turnhub.android.domain.ControllerHandle
import com.turnhub.android.domain.TablePlayer
import com.turnhub.android.protocol.GameProfile
import org.junit.Assert.assertEquals
import org.junit.Test

class TabletLayoutTest {

    @Test
    fun `two players face each other across the ends`() {
        assertEquals(Placement(near = listOf(0), far = listOf(1)), placements(2))
    }

    @Test
    fun `seats run clockwise from the near-left corner`() {
        // Five players: three on the near side, two on the far side.
        assertEquals(Placement(near = listOf(0, 4, 3), far = listOf(1, 2)), placements(5))
        assertEquals(Placement(near = listOf(0, 5, 4), far = listOf(1, 2, 3)), placements(6))
        assertEquals(Placement(near = listOf(0), far = emptyList()), placements(1))
        assertEquals(Placement(emptyList(), emptyList()), placements(0))
    }

    @Test
    fun `Yu-Gi-Oh! counts life in hundreds`() {
        assertEquals(100 to 1000, lifeSteps(GameProfile.YUGIOH))
        assertEquals(1 to 5, lifeSteps(GameProfile.MTG_COMMANDER))
    }

    @Test
    fun `a team's life sits on the first player still in`() {
        fun player(n: Int, out: Boolean, life: Int) = TablePlayer(
            playerNumber = n, label = "P$n", hasName = true, controller = ControllerHandle(8 + n), slot = 1,
            team = (n + 1) / 2, participantId = n.toLong(), eliminated = out, life = life, turnsCompleted = 0,
            commanderDamage = emptyList(), lifeRequest = null,
        )
        val unit = TableUnit("t1", listOf(player(1, true, 30), player(2, false, 30)))
        assertEquals(2, unit.lifeSeat.playerNumber)
        assertEquals("P1 & P2", unit.name)
    }
}
