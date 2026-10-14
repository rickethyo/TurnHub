package com.turnhub.android.standalone

import com.turnhub.android.protocol.GameProfile
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertSame
import org.junit.Assert.assertTrue
import org.junit.Test

class OfficialRulesTest {
    private fun magicTeams(players: Int) = (1..players).fold(StandaloneGame().setFormat(GameProfile.MTG)) { g, i -> g.addPlayer("P$i") }
        .setTwoHeadedGiant(true)

    @Test fun `official modes carry the comprehensive rules values and custom modes have none`() {
        assertEquals(listOf(30, 15, 4), officialRules(GameProfile.MTG, true)!!.let { listOf(it.life, it.poison, it.players) })
        assertEquals(listOf(40, 10, 21), officialRules(GameProfile.MTG_COMMANDER, false)!!.let { listOf(it.life, it.poison, it.commanderDamage) })
        assertEquals(20, officialRules(GameProfile.MTG, false)!!.life)
        assertNull(officialRules(GameProfile.MTG_COMMANDER, true))
        assertNull(officialRules(GameProfile.MTG_STAR_COMMANDER, false))
        assertNull(officialRules(GameProfile.GENERIC, false))
        assertTrue(modeRules(GameProfile.MTG_COMMANDER, true).title.contains("custom"))
    }

    @Test fun `official two-headed giant is two teams of two unless house rules are on`() {
        assertTrue(magicTeams(4).canStart)
        assertFalse(magicTeams(6).canStart)
        assertTrue(magicTeams(6).setHouseRules(true).canStart)
        // Two-Headed Giant Commander is a custom mode: any even table of four or more.
        assertTrue(magicTeams(6).setFormat(GameProfile.MTG_COMMANDER).canStart)
    }

    @Test fun `official life and poison are locked until house rules unlock them, and restored after`() {
        val lobby = magicTeams(4)
        assertSame(lobby, lobby.setStartingLife(50))
        assertEquals(30, lobby.startingLife)
        val house = lobby.setHouseRules(true).setStartingLife(50).setEliminationRule("poisonLimit", 20, 0)
        assertEquals(50, house.startingLife)
        assertEquals(20, house.poisonLimit)
        val official = house.setHouseRules(false)
        assertEquals(30, official.startingLife)
        assertEquals(15, official.poisonLimit)
        assertTrue(StandaloneTable.gameFromJson(StandaloneTable.gameToJson(house)).houseRules)
        val running = house.start(0, "g")
        assertSame(running, running.setHouseRules(false))
    }
}
