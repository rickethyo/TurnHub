package com.turnhub.android.ui.tablet

import com.turnhub.android.protocol.ProfileSummary
import com.turnhub.android.standalone.AtlasProfileLink
import com.turnhub.android.standalone.LocalCredential
import com.turnhub.android.standalone.LocalImportState
import com.turnhub.android.standalone.SavedLocalPlayer
import org.junit.Assert.assertEquals
import org.junit.Test

class BringPlayersTest {
    private val atlas = LocalImportState("THA-1", listOf(ProfileSummary("AAAA0001", "Ana on Atlas", true)))

    @Test fun `each local account says where it stands with this Atlas, by ID only`() {
        val linked = SavedLocalPlayer("l", "Lee", "BBBB0001", links = listOf(AtlasProfileLink("THA-1", "AAAA0001")))
        val elsewhere = SavedLocalPlayer("e", "Eve", "BBBB0002", links = listOf(AtlasProfileLink("THA-2", "CCCC0001")))
        val sameId = SavedLocalPlayer("a", "Someone else", "AAAA0001")
        val sameName = SavedLocalPlayer("n", "Ana on Atlas", "BBBB0003")
        val withPin = SavedLocalPlayer("p", "Pat", "BBBB0004", credential = LocalCredential.create("2468"))
        assertEquals(BringState.LINKED, bringState(linked, atlas))
        assertEquals(BringState.NEW_NO_PIN, bringState(elsewhere, atlas))
        assertEquals(BringState.ALREADY_THERE, bringState(sameId, atlas))
        // A matching name never counts as the same account.
        assertEquals(BringState.NEW_NO_PIN, bringState(sameName, atlas))
        assertEquals(BringState.NEW_WITH_PIN, bringState(withPin, atlas))
    }
}
