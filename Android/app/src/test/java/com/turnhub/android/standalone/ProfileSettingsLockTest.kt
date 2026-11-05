package com.turnhub.android.standalone

import org.junit.Assert.*
import org.junit.Test

class ProfileSettingsLockTest {
    @Test fun `room directory shares preset appearance without private image or verifier`() {
        val profile=SavedLocalPlayer("local", "Sam", "ABCDEF12",
            personalization=LocalPersonalization(color="#123456",avatar="crown",image="c2VjcmV0"),
            credential=LocalCredential.create("1234"))
        val table=StandaloneTable(MemoryStandaloneStore(StandaloneGame(),LocalLibrary(players=listOf(profile))))
        val item=table.roomProfiles().getJSONObject(0)
        assertEquals("#123456",item.getString("color"))
        assertEquals("crown",item.getString("avatar"))
        assertEquals(setOf("profileId","name","hasPin","color","avatar"),item.keys().asSequence().toSet())
    }
    @Test fun `locking settings keeps the profile and active seat but refuses edits until PIN unlock`() {
        val profile=SavedLocalPlayer("local", "Sam", "ABCDEF12", credential=LocalCredential.create("1234"))
        val game=StandaloneGame().addPlayer(profile.name,localId=profile.localId)
        val table=StandaloneTable(MemoryStandaloneStore(game,LocalLibrary(players=listOf(profile))))
        assertTrue(table.unlockAccount(profile.localId,"1234"))
        assertTrue(table.saveAccount(profile.localId,"New name",profile.personalization))
        val before=table.state.value
        table.lockAccount(profile.localId)
        assertFalse(table.accountUnlocked(profile.localId))
        assertFalse(table.saveAccount(profile.localId,"Wrong",profile.personalization))
        assertFalse(table.clearAccountCredential(profile.localId))
        assertEquals(before,table.state.value)
        assertFalse(table.unlockAccount(profile.localId,"9999"))
        assertTrue(table.unlockAccount(profile.localId,"1234"))
        assertEquals("ABCDEF12",table.state.value.library.players.single().profileId)
        assertEquals("New name",table.state.value.game.players.single().name)
    }
}
