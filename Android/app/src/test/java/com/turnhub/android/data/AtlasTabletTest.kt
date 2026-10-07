package com.turnhub.android.data

import com.turnhub.android.protocol.AccessibilitySettings
import com.turnhub.android.protocol.ControlResult
import com.turnhub.android.protocol.GameSettingsInfo
import com.turnhub.android.protocol.LedStyle
import com.turnhub.android.protocol.LoginResult
import com.turnhub.android.protocol.ProfileSummary
import com.turnhub.android.domain.TableSummaryMapper
import com.turnhub.android.protocol.SessionInfo
import com.turnhub.android.testing.Fixtures
import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/** Atlas's tablet routes as protocol/http-v1.md describes them; Bea (P0000002) keeps the PIN 1234. */
private class TabletAtlas : AtlasSessionTransport {
    val posts = mutableListOf<Pair<String, Map<String, String>>>()
    var tablet = false

    override suspend fun raw(method: String, path: String, token: String, fields: List<Pair<String, String>>): RawResponse {
        val f = fields.toMap()
        posts += path to f
        return when (path) {
            "/api/presence/request" -> RawResponse(200, """{"ok":true}""")
            "/api/presence/confirm" ->
                if (f["code"] == "123456") RawResponse(200, """{"ok":true}""")
                else RawResponse(403, """{"ok":false,"error":"That code is not the one Atlas shows"}""")
            "/api/tablet/enable" -> { tablet = true; RawResponse(200, """{"ok":true}""") }
            "/api/tablet/seat" -> when {
                f["name"] == "Ann" -> RawResponse(409, """{"ok":false,"error":"That name is taken"}""")
                f["name"] != null -> RawResponse(200, """{"ok":true,"profileId":"P0000009"}""")
                f["profileId"] == "P0000002" && f["pin"] == null ->
                    RawResponse(403, """{"ok":false,"pinRequired":true,"error":"Enter this player's PIN"}""")
                f["profileId"] == "P0000002" && f["pin"] != "1234" -> RawResponse(403, """{"ok":false,"error":"Wrong PIN"}""")
                else -> RawResponse(200, """{"ok":true,"profileId":"${f["profileId"]}"}""")
            }
            "/api/tablet/life" -> RawResponse(200, """{"ok":true,"status":"ACCEPTED"}""")
            else -> RawResponse(404, "{}")
        }
    }

    override suspend fun login(profileId: String, pin: String) = LoginResult("tablet-token", profileId)
    override suspend fun me(token: String) = SessionInfo("P0000001", "Host", 0, 0, 0, false, false, false, false, tablet = tablet)
    override suspend fun getProfiles() = listOf(ProfileSummary("P0000002", "Bea", hasPin = true))
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

class AtlasTabletTest {

    private suspend fun signedIn(atlas: TabletAtlas): Pair<AtlasPlayerSession, AtlasTablet> {
        val session = AtlasPlayerSession { atlas }
        session.signIn(AtlasEndpoint.DEFAULT, ProfileSummary("P0000001", "Host", hasPin = true), "0000")
        return session to AtlasTablet(session) { AtlasEndpoint.DEFAULT }
    }

    @Test
    fun `the code from Atlas turns this session into the tablet`() = runTest {
        val atlas = TabletAtlas()
        val (session, tablet) = signedIn(atlas)

        tablet.requestCode()
        assertEquals("/api/presence/request" to mapOf("purpose" to "tablet"), atlas.posts.last())
        assertTrue(tablet.state.value.codePrompt)

        tablet.confirmCode("12 34 56")
        assertEquals(listOf("/api/presence/confirm", "/api/tablet/enable"), atlas.posts.takeLast(2).map { it.first })
        assertFalse(tablet.state.value.codePrompt)
        val info = (session.state.value as PlayerSessionState.SignedIn).info
        assertTrue(info!!.tablet)
    }

    @Test
    fun `a wrong code is shown and the tablet stays off`() = runTest {
        val atlas = TabletAtlas()
        val (_, tablet) = signedIn(atlas)
        tablet.requestCode()
        tablet.confirmCode("000000")

        assertEquals("That code is not the one Atlas shows", tablet.state.value.message?.message)
        assertTrue(tablet.state.value.codePrompt)
        assertFalse(atlas.tablet)
    }

    @Test
    fun `a saved player with a PIN is asked for it, then seated`() = runTest {
        val atlas = TabletAtlas()
        val (_, tablet) = signedIn(atlas)

        tablet.seatSaved("P0000002", "Bea")
        assertEquals(TabletPinPrompt("P0000002", "Bea"), tablet.state.value.pinPrompt)
        assertNull(tablet.state.value.message)

        tablet.seatSaved("P0000002", "Bea", pin = "9999")
        assertNull(tablet.state.value.pinPrompt)
        assertEquals("Wrong PIN", tablet.state.value.message?.message)

        tablet.seatSaved("P0000002", "Bea", pin = "1234")
        assertEquals(mapOf("profileId" to "P0000002", "pin" to "1234"), atlas.posts.last().second)
        assertNull(tablet.state.value.message)
    }

    @Test
    fun `a new name is seated and a taken one is refused`() = runTest {
        val atlas = TabletAtlas()
        val (_, tablet) = signedIn(atlas)

        tablet.seatNew("  Cid ")
        assertEquals(mapOf("name" to "Cid"), atlas.posts.last { it.first == "/api/tablet/seat" }.second)
        assertEquals(listOf("Bea"), tablet.state.value.savedProfiles.map { it.name })

        tablet.seatNew("Ann")
        assertEquals("That name is taken", tablet.state.value.message?.message)
    }

    @Test
    fun `life carries the seat and the gathered change`() = runTest {
        val atlas = TabletAtlas()
        val (_, tablet) = signedIn(atlas)

        assertTrue(tablet.life(TabletSeat(9, 1), -7))
        assertEquals(mapOf("module" to "9", "slot" to "1", "delta" to "-7"), atlas.posts.last().second)
    }

    // --- changes made while Atlas wasn't answering ------------------------------------

    private val running = TableSummaryMapper.map(Fixtures.info(), Fixtures.state("running.response.json"))

    private fun change(delta: Int, at: Long = 1_000, participant: Long = 28, source: Int? = null, elapsed: Long = 100) =
        OfflineChange(participant, source = source, delta = delta, queuedAtMs = at, bootId = Fixtures.BOOT_ID, gameElapsedMs = elapsed)

    @Test
    fun `offline taps on one target add up, and cancel out`() = runTest {
        val (_, tablet) = signedIn(TabletAtlas())
        tablet.queueOffline(change(-1, at = 1_000))
        tablet.queueOffline(change(-1, at = 2_000))
        tablet.queueOffline(change(3, participant = 29))

        val offline = tablet.state.value.offline
        assertEquals(listOf(-2, 3), offline.map { it.delta })
        assertEquals(1_000L, offline.first().queuedAtMs)
        assertEquals(-2, tablet.state.value.offlineLife(28))
        assertEquals(2, tablet.state.value.waiting)

        tablet.queueOffline(change(2))
        assertEquals(listOf(29L), tablet.state.value.offline.map { it.participantId })
    }

    @Test
    fun `Commander damage kept offline costs life too`() = runTest {
        val (_, tablet) = signedIn(TabletAtlas())
        tablet.queueOffline(change(3, source = 2))
        assertEquals(-3, tablet.state.value.offlineLife(28))
        assertEquals(3, tablet.state.value.offlineCommander(28, 2, 1))
        assertEquals(0, tablet.state.value.offlineCommander(28, 2, 2))
    }

    @Test
    fun `on reconnect waiting changes go to Atlas with their age, quietly`() = runTest {
        val atlas = TabletAtlas()
        val (_, tablet) = signedIn(atlas)
        tablet.queueOffline(change(-4, at = 1_000))

        tablet.replayOffline(running, nowMs = 6_000)

        val (path, fields) = atlas.posts.last()
        assertEquals("/api/tablet/life", path)
        assertEquals("-4", fields["delta"])
        assertEquals("5000", fields["queuedMs"])
        assertEquals("1", fields["slot"])
        assertNull(tablet.state.value.message)
        // Still shown until a newer snapshot carries it, then forgotten.
        assertEquals(-4, tablet.state.value.offlineLife(28))
        assertEquals(0, tablet.state.value.waiting)
        tablet.settleOffline(running.revision)
        assertEquals(-4, tablet.state.value.offlineLife(28))
        tablet.settleOffline(running.revision + 1)
        assertTrue(tablet.state.value.offline.isEmpty())
    }

    @Test
    fun `changes from another boot, an earlier game or a refused one are dropped with a note`() = runTest {
        val atlas = TabletAtlas()
        val (_, tablet) = signedIn(atlas)
        tablet.queueOffline(change(-1).copy(bootId = Fixtures.OTHER_BOOT_ID))
        tablet.queueOffline(change(-2, participant = 29, elapsed = 50_000))
        tablet.queueOffline(change(1, source = 2))

        tablet.replayOffline(running, nowMs = 2_000)

        // Only the Commander change was sent; the fake Atlas refuses it.
        assertEquals(listOf("/api/tablet/commander"), atlas.posts.map { it.first }.filter { it.startsWith("/api/tablet/") })
        assertTrue(tablet.state.value.offline.isEmpty())
        assertTrue(tablet.state.value.message!!.message.startsWith("3 changes"))
    }
}
