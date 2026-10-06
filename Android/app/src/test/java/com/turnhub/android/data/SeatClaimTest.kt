package com.turnhub.android.data

import com.turnhub.android.protocol.AccessibilitySettings
import com.turnhub.android.protocol.ControlResult
import com.turnhub.android.protocol.GameSettingsInfo
import com.turnhub.android.protocol.LedStyle
import com.turnhub.android.protocol.LoginResult
import com.turnhub.android.protocol.ProfileSummary
import com.turnhub.android.protocol.SessionInfo
import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/** Atlas's seat claim routes: a request, then polls until the Sigil's Link phone press (after [pollsUntilPress] polls). */
private class ClaimAtlas(var pollsUntilPress: Int = 2, var requestError: String? = null) : AtlasSessionTransport {
    val requests = mutableListOf<Pair<String, Map<String, String>>>() // requester token, fields
    var polls = 0

    override suspend fun raw(method: String, path: String, token: String, fields: List<Pair<String, String>>): RawResponse =
        when {
            method == "POST" && path == "/api/session/request" -> {
                requests += token to fields.toMap()
                requestError?.let { RawResponse(403, """{"ok":false,"error":"$it"}""") }
                    ?: RawResponse(202, """{"ok":true,"status":"pending","requestId":"ab12","expiresMs":30000}""")
            }
            method == "GET" && path == "/api/session/poll?id=ab12" -> {
                polls++
                if (pollsUntilPress in 1..polls) {
                    RawResponse(200, """{"ok":true,"status":"approved","module":1,"slot":1,"token":"seat-token"}""")
                } else {
                    RawResponse(200, """{"ok":true,"status":"pending"}""")
                }
            }
            else -> RawResponse(404, "{}")
        }

    override suspend fun me(token: String): SessionInfo {
        if (token != "seat-token") throw AtlasException(AtlasFailure.SessionExpired)
        return SessionInfo("P0000002", "Sam", 1, 1, 2, true, true, false, false)
    }

    override suspend fun getProfiles() = emptyList<ProfileSummary>()
    override suspend fun login(profileId: String, pin: String): LoginResult = throw AtlasException(AtlasFailure.Rejected("no"))
    override suspend fun join(token: String): String? = null
    override suspend fun control(token: String, action: ControlAction, expectedRevision: Long?, expectedBootId: String?) =
        ControlResult(true, "ACCEPTED", null, null, null)
    override suspend fun getGameSettings(token: String): GameSettingsInfo = throw AtlasException(AtlasFailure.Rejected("no"))
    override suspend fun setTurnTimer(token: String, turnTimerMs: Long): String? = null
    override suspend fun getAccessibility(token: String): AccessibilitySettings = throw AtlasException(AtlasFailure.Rejected("no"))
    override suspend fun saveAccessibility(token: String, sigilSound: Boolean, ledStyle: LedStyle, longPressMs: Int, winHoldMs: Int, lifeApprovalMs: Int) =
        throw AtlasException(AtlasFailure.Rejected("no"))
    override suspend fun logout(token: String) {}
}

class SeatClaimTest {

    @Test
    fun `a Link phone press signs this phone in to the seat`() = runTest {
        val atlas = ClaimAtlas()
        val session = AtlasPlayerSession { atlas }
        session.claimSeat(AtlasEndpoint.DEFAULT, moduleId = 1, slot = 1, seatName = "Sigil 2 seat A")

        assertEquals("" to mapOf("module" to "1", "slot" to "1"), atlas.requests.single())
        val signedIn = session.state.value as PlayerSessionState.SignedIn
        assertEquals("P0000002", signedIn.profileId)
        val claim = session.claim.value!!
        assertFalse(claim.waiting)
        assertFalse(claim.isError)
        assertEquals("Linked. This phone now controls Sigil 2 seat A.", claim.message)
    }

    @Test
    fun `Atlas's refusal is shown and nothing is polled`() = runTest {
        val atlas = ClaimAtlas(requestError = "Sign in with this profile's PIN to use it in a browser")
        val session = AtlasPlayerSession { atlas }
        session.claimSeat(AtlasEndpoint.DEFAULT, 1, 1, "Sigil 2 seat A")

        assertEquals(0, atlas.polls)
        assertTrue(session.claim.value!!.isError)
        assertEquals("Sign in with this profile's PIN to use it in a browser", session.claim.value!!.message)
        assertTrue(session.state.value is PlayerSessionState.SignedOut)
    }

    @Test
    fun `no press before the window closes ends the claim`() = runTest {
        val atlas = ClaimAtlas(pollsUntilPress = 0)
        val session = AtlasPlayerSession { atlas }
        session.claimSeat(AtlasEndpoint.DEFAULT, 1, 1, "Sigil 2 seat A")

        assertTrue(atlas.polls > 0)
        assertTrue(session.claim.value!!.isError)
        assertTrue(session.state.value is PlayerSessionState.SignedOut)
        session.clearClaim()
        assertEquals(null, session.claim.value)
    }
}
