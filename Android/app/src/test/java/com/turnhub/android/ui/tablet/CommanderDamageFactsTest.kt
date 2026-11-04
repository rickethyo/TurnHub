package com.turnhub.android.ui.tablet

import com.turnhub.android.domain.TableSummaryMapper
import com.turnhub.android.protocol.CommanderDamage
import com.turnhub.android.protocol.GameProfile
import com.turnhub.android.testing.Fixtures
import org.junit.Assert.*
import org.junit.Test

class CommanderDamageFactsTest {
    private fun summary() = TableSummaryMapper.map(Fixtures.info(), Fixtures.state("running.response.json"), receivedAtMs = 1_000).let {
        it.copy(settings = it.settings.copy(profile = GameProfile.MTG_COMMANDER))
    }

    @Test fun `full source names partner damage and pending offline damage remain visible`() {
        val base = summary()
        val source = base.players[1].copy(label = "A much longer source name")
        val recipient = base.players[0].copy(commanderDamage = listOf(CommanderDamage(source.playerNumber, listOf(7, 3))))
        val table = base.copy(players = listOf(recipient, source))
        val facts = commanderDamageFacts(TableUnit("recipient", listOf(recipient)), table) { who, from, commander ->
            if (who == recipient.participantId && from == source.playerNumber && commander == 2) 4 else 0
        }
        assertEquals(1, facts.size)
        assertEquals(14, facts.single().total)
        assertEquals(source.label, facts.single().name)
        assertTrue(facts.single().text.contains(source.label))
    }

    @Test fun `shared life does not combine separate recipients commander damage or emit zero rows`() {
        val base = summary()
        val source = base.players[1]
        val first = base.players[0].copy(commanderDamage = listOf(CommanderDamage(source.playerNumber, listOf(9, 2))))
        val second = first.copy(playerNumber = 3, participantId = 333, commanderDamage = listOf(CommanderDamage(source.playerNumber, listOf(20, 0))))
        val table = base.copy(players = listOf(first, source, second))
        val team = TableUnit("team", listOf(first, second))
        assertEquals(listOf(11, 20), commanderDamageFacts(team, table) { _, _, _ -> 0 }.map { it.total })
        assertTrue(commanderDamageFacts(team, table.copy(settings = table.settings.copy(profile = GameProfile.POKEMON))) { _, _, _ -> 0 }.isEmpty())
    }
}
