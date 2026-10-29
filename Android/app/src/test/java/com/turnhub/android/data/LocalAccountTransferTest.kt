package com.turnhub.android.data

import com.turnhub.android.protocol.*
import com.turnhub.android.standalone.*
import com.turnhub.android.testing.Fixtures
import kotlinx.coroutines.test.runTest
import org.junit.Assert.*
import org.junit.Test

class LocalAccountTransferTest {
    private class Atlas : AtlasSessionTransport {
        var id = "C0DE1234"
        var status = 200
        var rejectLogin = false
        var rejectPersonalization = false
        var logouts = 0
        var preferenceWrites = 0
        var registration: Map<String, String>? = null
        override suspend fun raw(method: String, path: String, token: String, fields: List<Pair<String, String>>): RawResponse {
            if (path == "/api/profiles/link-key") return RawResponse(200, """{"ok":true,"linkKey":"0123456789ABCDEF0123456789ABCDEF"}""")
            registration = fields.toMap()
            return RawResponse(status, if (status == 200) """{"ok":true,"token":"token","profileId":"$id"}""" else """{"error":"ID exists"}""")
        }
        override suspend fun login(profileId: String, pin: String): LoginResult {
            if (rejectLogin) throw AtlasException(AtlasFailure.Rejected("Wrong PIN"))
            id = profileId
            return LoginResult("verified", id)
        }
        override suspend fun me(token: String) = SessionInfo(id, "Owner", 8, 1, 0, false, false, false, false)
        override suspend fun logout(token: String) { logouts++ }
        override suspend fun getProfiles() = emptyList<ProfileSummary>()
        override suspend fun join(token: String): String? = null
        override suspend fun control(token: String, action: ControlAction, expectedRevision: Long?, expectedBootId: String?) = error("unused")
        override suspend fun getGameSettings(token: String) = error("unused")
        override suspend fun setTurnTimer(token: String, turnTimerMs: Long): String? = null
        override suspend fun getAccessibility(token: String) = AtlasWireParser.parseAccessibility(Fixtures.text("accessibility.response.json"))
        override suspend fun saveAccessibility(token: String, sigilSound: Boolean, ledStyle: LedStyle, longPressMs: Int, winHoldMs: Int, lifeApprovalMs: Int) = getAccessibility(token)
        override suspend fun savePersonalization(token: String, color: String?, avatar: Int?): Personalization {
            preferenceWrites++
            if (rejectPersonalization) throw AtlasException(AtlasFailure.Rejected("No card"))
            return Personalization(color, avatar ?: 0, true)
        }
    }
    private val player = SavedLocalPlayer("local-uuid", "Owner", "C0DE1234", LocalPersonalization("#112233", "cat"))

    @Test fun `creation preserves public ID and transfers presentation without keeping the token`() = runTest {
        val atlas = Atlas()
        val result = LocalAccountTransfer(atlas).verify(player, null, "1234")
        assertEquals(player.profileId, result.profileId)
        assertEquals(player.profileId, atlas.registration?.get("profileId"))
        assertEquals(1, atlas.preferenceWrites)
        assertEquals(1, atlas.logouts)
    }

    @Test fun `matching public ID collision is refused rather than overwritten or automatically merged`() = runTest {
        val atlas = Atlas().apply { status = 409 }
        assertTrue(runCatching { LocalAccountTransfer(atlas).verify(player, null, "1234") }.isFailure)
        assertEquals(0, atlas.preferenceWrites)
        assertEquals(0, atlas.logouts)
    }

    @Test fun `merging requires successful credentials and retains Atlas preferences`() = runTest {
        val atlas = Atlas().apply { rejectLogin = true }
        assertTrue(runCatching { LocalAccountTransfer(atlas).verify(player, "AABBCCDD", "1234") }.isFailure)
        atlas.rejectLogin = false
        val result = LocalAccountTransfer(atlas).verify(player, "AABBCCDD", "1234")
        assertEquals("AABBCCDD", result.profileId)
        assertNull(atlas.registration)
        assertEquals(0, atlas.preferenceWrites)
        assertEquals(1, atlas.logouts)
    }

    @Test fun `partial personalization failure still returns the created identity for a durable link`() = runTest {
        val atlas = Atlas().apply { rejectPersonalization = true }
        val result = LocalAccountTransfer(atlas).verify(player, null, "1234")
        assertEquals(player.profileId, result.profileId)
        assertNotNull(result.warning)
        assertEquals(1, atlas.logouts)
    }

    @Test fun `older Atlas assigning a different ID cannot silently establish the wrong link`() = runTest {
        val atlas = Atlas().apply { id = "AABBCCDD" }
        assertTrue(runCatching { LocalAccountTransfer(atlas).verify(player, null, "1234") }.isFailure)
        assertEquals(1, atlas.logouts)
    }
}
