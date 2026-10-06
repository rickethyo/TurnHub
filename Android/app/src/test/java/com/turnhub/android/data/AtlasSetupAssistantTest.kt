package com.turnhub.android.data

import com.turnhub.android.protocol.AccessibilitySettings
import com.turnhub.android.protocol.ControlResult
import com.turnhub.android.protocol.GameSettingsInfo
import com.turnhub.android.protocol.LedStyle
import com.turnhub.android.protocol.LoginResult
import com.turnhub.android.protocol.ProfileSummary
import com.turnhub.android.protocol.SessionInfo
import com.turnhub.android.protocol.SetupStage
import com.turnhub.android.protocol.SetupStatus
import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import org.junit.Test

/** A small Atlas: accounts, table codes, setup stage, devices and updates, over the session routes. */
private class FakeAtlas : AtlasSessionTransport {
    val calls = mutableListOf<String>()
    var stage = SetupStage.WELCOME
    var admin: String? = null
    val profiles = mutableMapOf<String, Pair<String, String>>() // id -> name, pin
    val tokens = mutableMapOf<String, String>() // token -> profile
    val verified = mutableSetOf<String>()
    var codeFor: String? = null
    var wifiPassword: String? = null
    var firmware = "0.6.0"
    var sigilFirmware = "0.8.0"
    var stagedSigilPackage = false
    var failUpload: AtlasFailure? = null

    fun restart() {
        tokens.clear()
        verified.clear()
        codeFor = null
    }

    private fun who(token: String) = tokens[token]

    override suspend fun getProfiles() = profiles.map { (id, v) -> ProfileSummary(id, v.first, hasPin = true) }

    override suspend fun register(name: String, pin: String): LoginResult {
        val id = "P%07d".format(profiles.size + 1)
        profiles[id] = name to pin
        return login(id, pin)
    }

    override suspend fun login(profileId: String, pin: String): LoginResult {
        val profile = profiles[profileId] ?: throw AtlasException(AtlasFailure.Rejected("Unknown profile"))
        if (profile.second != pin) throw AtlasException(AtlasFailure.Rejected("Wrong PIN"))
        val token = "t${tokens.size}-$profileId"
        tokens[token] = profileId
        return LoginResult(token, profileId)
    }

    override suspend fun me(token: String): SessionInfo {
        val id = who(token) ?: throw AtlasException(AtlasFailure.SessionExpired)
        return SessionInfo(id, profiles[id]?.first, 8, 1, 0, false, false, false, false,
            permissions = if (id == admin) 1 else 0)
    }

    override suspend fun getSetup() = SetupStatus(stage, admin != null, wifiPassword == null, "TurnHub-Atlas")

    override suspend fun raw(method: String, path: String, token: String, fields: List<Pair<String, String>>): RawResponse {
        calls += "$method $path"
        val id = who(token) ?: return RawResponse(401, """{"error":"Sign in first"}""")
        val arg = fields.toMap()
        return when ("$method $path") {
            "GET /api/presence" -> RawResponse(200, """{"verified":${id in verified},"remainingMs":${if (id in verified) 600000 else 0}}""")
            "POST /api/presence/request" -> { codeFor = id; RawResponse(200, """{"ok":true}""") }
            "POST /api/presence/confirm" ->
                if (codeFor == id && arg["code"] == "123456") {
                    verified += id; codeFor = null; RawResponse(200, """{"ok":true}""")
                } else {
                    RawResponse(400, """{"error":"That is not the code on the Atlas screen"}""")
                }
            "POST /api/accounts/setup" ->
                if (admin == null && id in verified) { admin = id; RawResponse(200, """{"ok":true}""") }
                else RawResponse(409, """{"error":"Admin setup is already complete"}""")
            "GET /api/devices" -> RawResponse(200, """{"devices":[
                {"id":0,"label":"Kitchen","online":true,"firmware":"$sigilFirmware","display":"oled","capabilities":80},
                {"id":5,"label":"Harness","online":true,"firmware":"0.1.0","display":"oled","capabilities":208}]}""")
            "POST /api/sigil-update" -> { sigilFirmware = "0.9.0"; RawResponse(200, """{"ok":true}""") }
            "GET /api/sigil-firmware" -> RawResponse(200, """{"stage":"done","message":"Updated"}""")
            "POST /api/setup/finish" -> when {
                id != admin -> RawResponse(403, """{"error":"Admin permission required"}""")
                id !in verified -> RawResponse(403, """{"ok":false,"presenceRequired":true,"error":"Verify at the table first"}""")
                else -> { wifiPassword = arg["password"]; stage = SetupStage.FINISHED; RawResponse(200, """{"ok":true,"restarting":true}""") }
            }
            else -> RawResponse(404, """{"error":"no route"}""")
        }
    }

    override suspend fun upload(path: String, token: String, field: String, fileName: String, bytes: ByteArray): RawResponse {
        calls += "UPLOAD $path $fileName"
        failUpload?.let { throw AtlasException(it) }
        val id = who(token) ?: return RawResponse(401, "{}")
        if (id != admin || id !in verified) return RawResponse(403, """{"error":"Update not armed"}""")
        return when (path) {
            "/api/firmware" -> { firmware = "0.7.0"; RawResponse(200, """{"ok":true}""") }
            "/api/sigil-firmware" -> { stagedSigilPackage = true; RawResponse(200, """{"ok":true}""") }
            else -> RawResponse(404, "{}")
        }
    }

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

private class FakeReleases(var feed: FirmwareReleaseFeed?) : FirmwareReleaseSource {
    val downloads = mutableListOf<String>()
    override suspend fun latestFeed() = feed ?: throw NoReleaseException()
    override suspend fun download(pkg: FirmwarePackage, onProgress: (Long) -> Unit): ByteArray {
        downloads += pkg.file
        return ByteArray(pkg.size.toInt())
    }
}

private class FakeHost(val atlas: FakeAtlas) : SetupHost {
    var restarts = 0
    var saved: Pair<String, String>? = null
    override fun endpoint() = AtlasEndpoint.DEFAULT
    override fun atlasFirmware() = atlas.firmware
    override fun wifiPasswordChanged(ssid: String, password: String) { saved = ssid to password }
    override suspend fun reconnectAfterRestart(): Boolean {
        restarts++
        atlas.restart()
        return true
    }
}

class AtlasSetupAssistantTest {

    private val feed = FirmwareReleaseFeed(
        "0.9.0",
        listOf(
            FirmwarePackage(FirmwareProduct.ATLAS, FirmwareVersion(0, 7, 0), "atlas-0.7.0.thfw", 8, "0".repeat(64)),
            FirmwarePackage(FirmwareProduct.SIGIL_OLED, FirmwareVersion(0, 9, 0), "sigil-oled-0.9.0.thfw", 4, "0".repeat(64)),
        ),
    )

    private fun setup(atlas: FakeAtlas = FakeAtlas(), releases: FakeReleases = FakeReleases(feed)): Triple<AtlasSetupAssistant, FakeAtlas, FakeHost> {
        val host = FakeHost(atlas)
        val session = AtlasPlayerSession { atlas }
        return Triple(AtlasSetupAssistant(session, releases, host, pollMs = 1), atlas, host)
    }

    @Test
    fun `a new Atlas is set up end to end with every device updated`() = runTest {
        val (assistant, atlas, host) = setup()
        assistant.check()
        assertTrue(assistant.state.value.visible)
        assertEquals(SetupStep.WELCOME, assistant.state.value.step)

        assistant.next()
        assertEquals(SetupStep.ACCOUNT, assistant.state.value.step)
        assistant.createAccount("Owner", "12")
        assertEquals(ProfileSecret.RULE, assistant.state.value.error)
        assistant.createAccount("Owner", "2468")
        assertEquals(SetupStep.TABLE_CODE, assistant.state.value.step)

        assistant.requestCode()
        assertTrue(assistant.state.value.codeShowing)
        assistant.confirmCode("111 111")
        assertNotNull(assistant.state.value.error)
        assistant.confirmCode("123 456")
        assertEquals(atlas.profiles.keys.single(), atlas.admin)
        assertEquals(SetupStep.SIGILS, assistant.state.value.step)
        assertEquals(listOf("Kitchen"), assistant.state.value.sigils.map { it.label }) // The harness is hidden.

        assistant.sigilsDone()
        assertEquals(SetupStep.UPDATES, assistant.state.value.step)
        val ready = assistant.state.value.updates as UpdatesState.Ready
        assertEquals(listOf("Atlas", "Kitchen"), ready.plan.pending.map { it.label })

        // Atlas first: it restarts, so the app signs in again and asks for a new code.
        assistant.installUpdates()
        assertEquals("0.7.0", atlas.firmware)
        assertEquals(1, host.restarts)
        assertTrue(assistant.state.value.codeShowing)
        assistant.confirmCode("123456")
        val finished = assistant.state.value.updates as UpdatesState.Finished
        assertTrue(finished.lines.all { it.done })
        assertEquals("0.9.0", atlas.sigilFirmware)

        assistant.next()
        assertEquals(SetupStep.WIFI, assistant.state.value.step)
        assistant.finish(WifiCredentials.DEFAULT_ATLAS_PASSPHRASE)
        assertEquals(null, atlas.wifiPassword)
        assistant.finish("our-table-pw")
        assertEquals("our-table-pw", atlas.wifiPassword)
        assertEquals("TurnHub-Atlas" to "our-table-pw", host.saved)
        assertEquals(SetupStep.DONE, assistant.state.value.step)

        assistant.close()
        assertFalse(assistant.state.value.visible)
    }

    @Test
    fun `an upload Atlas cuts off ends the update step instead of crashing`() = runTest {
        val (assistant, atlas) = setup()
        assistant.check()
        assistant.next()
        assistant.createAccount("Owner", "2468")
        assistant.requestCode()
        assistant.confirmCode("123456")
        assistant.sigilsDone()
        atlas.failUpload = AtlasFailure.Timeout("SocketTimeoutException: timed out")

        assistant.installUpdates()

        val finished = assistant.state.value.updates as UpdatesState.Finished
        assertTrue(finished.lines.all { it.failed && !it.done })
        assertFalse(assistant.state.value.busy)
    }

    @Test
    fun `a factory reset Atlas starts setup over and asks for its code as the step opens`() = runTest {
        val (assistant, atlas) = setup()
        assistant.check()
        assistant.next()
        assistant.createAccount("Owner", "2468")
        assertTrue(assistant.state.value.codeShowing) // Asked for as the step opened.
        assistant.confirmCode("123456")
        assistant.sigilsDone()
        assistant.skipUpdates()
        assistant.next()
        assertEquals(SetupStep.WIFI, assistant.state.value.step)

        // Factory reset: Atlas forgets everything and shows Welcome again.
        atlas.restart()
        atlas.profiles.clear()
        atlas.admin = null
        atlas.stage = SetupStage.WELCOME
        assistant.close()
        assistant.check()
        assistant.next()
        assistant.createAccount("Owner", "2468")
        assistant.confirmCode("123456")

        assertEquals(SetupStep.SIGILS, assistant.state.value.step)
    }

    @Test
    fun `a set-up Atlas shows no setup`() = runTest {
        val (assistant, atlas) = setup()
        atlas.stage = SetupStage.COMPLETE
        assistant.check()
        assertFalse(assistant.state.value.visible)
    }

    @Test
    fun `no published release lets setup carry on`() = runTest {
        val (assistant, atlas) = setup(releases = FakeReleases(null))
        assistant.check()
        assistant.next()
        assistant.createAccount("Owner", "2468")
        assistant.requestCode()
        assistant.confirmCode("123456")
        assistant.sigilsDone()
        assertTrue(assistant.state.value.updates is UpdatesState.Unavailable)
        assistant.skipUpdates()
        assistant.next()
        assertEquals(SetupStep.WIFI, assistant.state.value.step)
        assertTrue(atlas.calls.none { it.startsWith("UPLOAD") })
    }

    @Test
    fun `an expired table code is asked for again before finishing`() = runTest {
        val (assistant, atlas) = setup(releases = FakeReleases(null))
        assistant.check()
        assistant.next()
        assistant.createAccount("Owner", "2468")
        assistant.requestCode()
        assistant.confirmCode("123456")
        assistant.sigilsDone()
        assistant.skipUpdates()
        assistant.next()
        atlas.verified.clear()
        assistant.finish("our-table-pw")
        assertTrue(assistant.state.value.codeShowing)
        assistant.confirmCode("123456")
        assertEquals("our-table-pw", atlas.wifiPassword)
        assertEquals(SetupStep.DONE, assistant.state.value.step)
    }

    @Test
    fun `another account cannot take over a table that has an Admin`() = runTest {
        val atlas = FakeAtlas()
        atlas.profiles["P0000009"] = "Owner" to "1111"
        atlas.admin = "P0000009"
        val (assistant) = setup(atlas)
        assistant.check()
        assistant.next()
        assistant.createAccount("Guest", "2468")
        assistant.requestCode()
        assistant.confirmCode("123456")
        assertEquals(SetupStep.TABLE_CODE, assistant.state.value.step)
        assertTrue(assistant.state.value.error!!.contains("already has an Admin"))
    }
}
