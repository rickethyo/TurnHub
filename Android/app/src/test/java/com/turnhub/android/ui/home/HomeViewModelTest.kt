package com.turnhub.android.ui.home

import com.turnhub.android.data.AtlasEndpoint
import com.turnhub.android.data.AtlasFailure
import com.turnhub.android.data.AtlasPlayerSession
import com.turnhub.android.data.AtlasRepository
import com.turnhub.android.data.AtlasSessionTransport
import com.turnhub.android.data.AtlasWifiLink
import com.turnhub.android.data.ControlAction
import com.turnhub.android.data.WifiCredentialStore
import com.turnhub.android.data.WifiCredentials
import com.turnhub.android.data.WifiJoinResult
import com.turnhub.android.domain.TableSummary
import com.turnhub.android.domain.TableSummaryMapper
import com.turnhub.android.testing.Fixtures
import com.turnhub.android.protocol.AtlasConnectionState
import com.turnhub.android.data.AtlasException
import com.turnhub.android.protocol.ControlResult
import com.turnhub.android.protocol.GameProfile
import com.turnhub.android.protocol.GameSettingsInfo
import com.turnhub.android.protocol.LoginResult
import com.turnhub.android.protocol.ProfileSummary
import com.turnhub.android.protocol.SessionInfo
import com.turnhub.android.protocol.TableSettings
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.test.TestScope
import kotlinx.coroutines.test.UnconfinedTestDispatcher
import kotlinx.coroutines.test.resetMain
import kotlinx.coroutines.test.runTest
import kotlinx.coroutines.test.setMain
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test

private class RecordingRepository : AtlasRepository {
    override val connectionState = MutableStateFlow(AtlasConnectionState.DISCONNECTED)
    override val endpoint = MutableStateFlow<AtlasEndpoint?>(null)
    override val tableSummary = MutableStateFlow<TableSummary?>(null)
    override val failure = MutableStateFlow<AtlasFailure?>(null)
    val connects = mutableListOf<AtlasEndpoint>()
    var disconnects = 0

    override suspend fun connect(endpoint: AtlasEndpoint) {
        connects += endpoint
    }

    override suspend fun disconnect() {
        disconnects++
    }
}

private class FakeWifiLink : AtlasWifiLink {
    override val joinedSsid = MutableStateFlow<String?>(null)
    val joins = mutableListOf<WifiCredentials>()
    var results = ArrayDeque<WifiJoinResult>()
    var gate: CompletableDeferred<Unit>? = null
    var releases = 0

    override suspend fun join(credentials: WifiCredentials): WifiJoinResult {
        joins += credentials
        gate?.await()
        return results.removeFirstOrNull() ?: WifiJoinResult.Joined
    }

    override fun release() {
        releases++
    }
}

private class MemoryCredentialStore : WifiCredentialStore {
    val saved = mutableMapOf<String, WifiCredentials>()
    var last: String? = null

    override fun lastSsid(): String? = last
    override fun load(ssid: String): WifiCredentials? = saved[ssid]
    override fun save(credentials: WifiCredentials) {
        saved[credentials.ssid] = credentials
        last = credentials.ssid
    }
}

/** Records every request so tests can prove each tap sends exactly one. */
private class FakeSessionTransport : AtlasSessionTransport {
    val calls = mutableListOf<String>()
    var participating = false
    var host = true
    var controlResult: (ControlAction) -> ControlResult = { ControlResult(true, "ACCEPTED", "OK", null, null) }
    var controlFailure: AtlasException? = null
    val settings = GameSettingsInfo(
        settings = TableSettings(GameProfile.GENERIC, 40, 0),
        available = true,
        canEdit = true,
        turnTimerPresetsMs = listOf(0, 60_000, 120_000, 180_000, 300_000),
        turnTimerMinMs = 15_000,
        turnTimerMaxMs = 3_600_000,
    )

    override suspend fun getProfiles(): List<ProfileSummary> = listOf(ProfileSummary("p1", "Ricky", hasPin = true))
    override suspend fun login(profileId: String, pin: String): LoginResult =
        LoginResult("token", profileId).also { calls += "login $profileId" }
    override suspend fun me(token: String): SessionInfo =
        SessionInfo("p1", "Ricky", 8, 1, if (participating) 1 else 0, participating, host, false, false)
    override suspend fun join(token: String): String? {
        calls += "join"
        participating = true
        return "Ready at the table"
    }
    override suspend fun control(
        token: String,
        action: ControlAction,
        expectedRevision: Long?,
        expectedBootId: String?,
    ): ControlResult {
        calls += "$action rev=$expectedRevision boot=$expectedBootId"
        controlFailure?.let { throw it }
        return controlResult(action)
    }
    override suspend fun getGameSettings(token: String): GameSettingsInfo = settings
    override suspend fun setTurnTimer(token: String, turnTimerMs: Long): String? {
        calls += "timer $turnTimerMs"
        return null
    }
    override suspend fun logout(token: String) {
        calls += "logout"
    }
}

@OptIn(ExperimentalCoroutinesApi::class)
class HomeViewModelTest {

    private val repository = RecordingRepository()
    private val link = FakeWifiLink()
    private val store = MemoryCredentialStore()
    private val sessionTransport = FakeSessionTransport()
    private val playerSession = AtlasPlayerSession { sessionTransport }
    private val defaultCredentials =
        WifiCredentials(WifiCredentials.DEFAULT_ATLAS_SSID, WifiCredentials.DEFAULT_ATLAS_PASSPHRASE)

    @Before
    fun setUp() = Dispatchers.setMain(UnconfinedTestDispatcher())

    @After
    fun tearDown() = Dispatchers.resetMain()

    private fun TestScope.viewModel(): HomeViewModel {
        val viewModel = HomeViewModel({ repository }, link, store, playerSession)
        backgroundScope.launch(UnconfinedTestDispatcher(testScheduler)) { viewModel.uiState.collect {} }
        return viewModel
    }

    @Test
    fun `new Atlas joins with the shipped default, saves it, then connects`() = runTest {
        val viewModel = viewModel()

        assertEquals("http://192.168.4.1", viewModel.uiState.value.endpointText)
        viewModel.onConnectClicked()

        assertEquals(listOf(defaultCredentials), link.joins)
        assertEquals(defaultCredentials, store.saved[WifiCredentials.DEFAULT_ATLAS_SSID])
        assertEquals(listOf(AtlasEndpoint.DEFAULT), repository.connects)
        assertNull(viewModel.uiState.value.wifiPrompt)
    }

    @Test
    fun `a saved password is used instead of the default`() = runTest {
        val saved = WifiCredentials("TurnHub-Atlas", "owner-chosen-pass")
        store.save(saved)
        val viewModel = viewModel()

        viewModel.onConnectClicked()

        assertEquals(listOf(saved), link.joins)
        assertEquals(1, repository.connects.size)
    }

    @Test
    fun `shows joining state while Android connects`() = runTest {
        link.gate = CompletableDeferred()
        val viewModel = viewModel()

        viewModel.onConnectClicked()
        assertEquals("TurnHub-Atlas", viewModel.uiState.value.joiningSsid)
        assertEquals(false, viewModel.uiState.value.endpointEditable)
        viewModel.onConnectClicked() // Ignored while joining.
        assertEquals(1, link.joins.size)

        link.gate!!.complete(Unit)
        assertNull(viewModel.uiState.value.joiningSsid)
        assertEquals(1, repository.connects.size)
    }

    @Test
    fun `failed join asks for the password and saves only what works`() = runTest {
        link.results.addLast(WifiJoinResult.Unavailable)
        val viewModel = viewModel()

        viewModel.onConnectClicked()

        val prompt = viewModel.uiState.value.wifiPrompt!!
        assertEquals("TurnHub-Atlas", prompt.ssid)
        assertTrue(prompt.message.contains("Couldn't join TurnHub-Atlas"))
        assertTrue(repository.connects.isEmpty())
        assertTrue(store.saved.isEmpty())

        viewModel.onWifiPasswordSubmitted("TurnHub-Atlas", "correct-horse")

        assertEquals(WifiCredentials("TurnHub-Atlas", "correct-horse"), link.joins.last())
        assertEquals("correct-horse", store.saved["TurnHub-Atlas"]!!.passphrase)
        assertEquals(1, repository.connects.size)
        assertNull(viewModel.uiState.value.wifiPrompt)
    }

    @Test
    fun `a wrong password keeps asking and saves nothing`() = runTest {
        link.results.addAll(listOf(WifiJoinResult.Unavailable, WifiJoinResult.Unavailable))
        val viewModel = viewModel()

        viewModel.onConnectClicked()
        viewModel.onWifiPasswordSubmitted("TurnHub-Atlas", "wrong-password")

        assertTrue(viewModel.uiState.value.wifiPrompt != null)
        assertTrue(store.saved.isEmpty())
        assertTrue(repository.connects.isEmpty())
    }

    @Test
    fun `invalid prompt input is explained without trying to join`() = runTest {
        link.results.addLast(WifiJoinResult.Unavailable)
        val viewModel = viewModel()
        viewModel.onConnectClicked()

        viewModel.onWifiPasswordSubmitted("TurnHub-Atlas", "short")
        assertTrue(viewModel.uiState.value.wifiPrompt!!.message.contains("8 to 63"))
        viewModel.onWifiPasswordSubmitted("  ", "long-enough")
        assertTrue(viewModel.uiState.value.wifiPrompt!!.message.contains("network name"))
        assertEquals(1, link.joins.size)
    }

    @Test
    fun `a renamed network with no saved password asks first`() = runTest {
        store.last = "TurnHub-Setup-A1B2"
        val viewModel = viewModel()

        viewModel.onConnectClicked()

        assertEquals("TurnHub-Setup-A1B2", viewModel.uiState.value.wifiPrompt!!.ssid)
        assertTrue(link.joins.isEmpty())
    }

    @Test
    fun `already on the Wi-Fi skips joining`() = runTest {
        link.results.addLast(WifiJoinResult.Unavailable)
        val viewModel = viewModel()
        viewModel.onConnectClicked()

        viewModel.onUseCurrentWifi()

        assertEquals(1, link.joins.size)
        assertEquals(listOf(AtlasEndpoint.DEFAULT), repository.connects)
        assertNull(viewModel.uiState.value.wifiPrompt)
    }

    @Test
    fun `cancelling the prompt does nothing`() = runTest {
        link.results.addLast(WifiJoinResult.Unavailable)
        val viewModel = viewModel()
        viewModel.onConnectClicked()

        viewModel.onWifiPromptDismissed()

        assertNull(viewModel.uiState.value.wifiPrompt)
        assertTrue(repository.connects.isEmpty())
    }

    @Test
    fun `non-access-point addresses connect without joining Wi-Fi`() = runTest {
        val viewModel = viewModel()

        viewModel.onEndpointChanged("10.0.0.5:8080/")
        viewModel.onConnectClicked()

        assertTrue(link.joins.isEmpty())
        assertEquals("http://10.0.0.5:8080", repository.connects.single().baseUrl)
        assertEquals("http://10.0.0.5:8080", viewModel.uiState.value.endpointText)
    }

    @Test
    fun `invalid endpoint shows an error and does not connect`() = runTest {
        val viewModel = viewModel()

        viewModel.onEndpointChanged("https://192.168.4.1")
        viewModel.onConnectClicked()

        assertTrue(repository.connects.isEmpty())
        assertTrue(link.joins.isEmpty())
        assertTrue(viewModel.uiState.value.errorMessage!!.contains("http://"))

        viewModel.onEndpointChanged("192.168.4.1")
        assertNull(viewModel.uiState.value.errorMessage)
    }

    @Test
    fun `denied local network permission explains itself and offers settings`() = runTest {
        val viewModel = viewModel()

        viewModel.onLocalNetworkPermissionDenied()

        val state = viewModel.uiState.value
        assertTrue(state.errorMessage!!.contains("Nearby devices"))
        assertTrue(state.offerAppSettings)
        assertTrue(repository.connects.isEmpty())

        // Granted later: connecting clears the message.
        viewModel.onConnectClicked()
        assertNull(viewModel.uiState.value.errorMessage)
        assertEquals(false, viewModel.uiState.value.offerAppSettings)
        assertEquals(1, repository.connects.size)
    }

    @Test
    fun `disconnect and lost connections give the Wi-Fi back`() = runTest {
        val viewModel = viewModel()
        viewModel.onConnectClicked()
        repository.connectionState.value = AtlasConnectionState.CONNECTED

        viewModel.onDisconnectClicked()
        assertEquals(1, repository.disconnects)
        assertTrue(link.releases >= 1)

        val before = link.releases
        repository.connectionState.value = AtlasConnectionState.CONNECTED
        repository.connectionState.value = AtlasConnectionState.DISCONNECTED // Lost.
        assertEquals(before + 1, link.releases)
    }

    // --- playing from this phone ------------------------------------------------

    private fun table(name: String, revision: Long, edit: org.json.JSONObject.() -> Unit = {}) =
        TableSummaryMapper.map(Fixtures.info(), Fixtures.state(name) { put("revision", revision); edit() })

    /** Player 1 (handle 8, this phone) seated in the lobby. */
    private fun seatedLobby(revision: Long) = table("running.response.json", revision) {
        put("state", "LOBBY")
        put("activePlayer", org.json.JSONObject.NULL)
    }

    private fun TestScope.connectedViewModel(summary: TableSummary): HomeViewModel {
        val viewModel = viewModel()
        repository.endpoint.value = AtlasEndpoint.DEFAULT
        repository.connectionState.value = AtlasConnectionState.CONNECTED
        repository.tableSummary.value = summary
        return viewModel
    }

    private fun HomeViewModel.signIn(pin: String = "1234") {
        onPlayFromPhoneClicked()
        onSignInSubmitted(uiState.value.signIn!!.profiles.single(), pin)
    }

    @Test
    fun `phone player signs in, joins, passes, pauses and signs out`() = runTest {
        val viewModel = connectedViewModel(table("lobby.response.json", 1))

        viewModel.onPlayFromPhoneClicked()
        val prompt = viewModel.uiState.value.signIn!!
        viewModel.onSignInSubmitted(prompt.profiles.single(), "12")
        assertTrue(viewModel.uiState.value.signIn!!.error!!.contains("4 to 8"))
        assertTrue(sessionTransport.calls.isEmpty())

        viewModel.onSignInSubmitted(prompt.profiles.single(), "1234")
        assertNull(viewModel.uiState.value.signIn)
        assertTrue(viewModel.uiState.value.player!!.canJoin)

        viewModel.onJoinClicked()
        repository.tableSummary.value = seatedLobby(2)
        assertEquals(false, viewModel.uiState.value.player!!.canJoin)

        // Atlas starts the game with this phone's player active.
        repository.tableSummary.value = table("running.response.json", 4)
        val running = viewModel.uiState.value.player!!
        assertTrue(running.canPass && running.canPauseResume)

        viewModel.onPassClicked()
        viewModel.onPauseResumeClicked()
        viewModel.onSignOutClicked()

        assertEquals(
            listOf(
                "login p1",
                "join",
                "PASS rev=4 boot=${Fixtures.BOOT_ID}",
                "PAUSE_RESUME rev=4 boot=${Fixtures.BOOT_ID}",
                "logout",
            ),
            sessionTransport.calls,
        )
        assertEquals(false, viewModel.uiState.value.player!!.signedIn)
    }

    @Test
    fun `a stale tap is reported, not retried`() = runTest {
        sessionTransport.participating = true
        sessionTransport.controlResult = { ControlResult(false, "CONFLICT", "Stale revision", 9, Fixtures.BOOT_ID) }
        val viewModel = connectedViewModel(table("running.response.json", 4))
        viewModel.signIn()

        viewModel.onPassClicked()

        assertEquals(1, sessionTransport.calls.count { it.startsWith("PASS") })
        val feedback = viewModel.uiState.value.player!!.feedback!!
        assertTrue(feedback.isError && feedback.message.contains("table changed"))
    }

    @Test
    fun `an ambiguous timeout is never replayed`() = runTest {
        sessionTransport.participating = true
        sessionTransport.controlFailure = AtlasException(AtlasFailure.Timeout("SocketTimeoutException"))
        val viewModel = connectedViewModel(table("running.response.json", 4))
        viewModel.signIn()

        viewModel.onPassClicked()

        assertEquals(1, sessionTransport.calls.count { it.startsWith("PASS") })
        assertTrue(viewModel.uiState.value.player!!.feedback!!.message.contains("didn't confirm"))
    }

    @Test
    fun `the host sets the turn timer in the lobby`() = runTest {
        sessionTransport.participating = true
        val viewModel = connectedViewModel(seatedLobby(2))
        viewModel.signIn()

        val editor = viewModel.uiState.value.player!!.timerEditor!!
        assertEquals(listOf(0L, 60_000L, 120_000L, 180_000L, 300_000L), editor.presetsMs)
        viewModel.onTurnTimerChosen(120_000)

        assertEquals("timer 120000", sessionTransport.calls.last())
        assertEquals("Turn timer saved on Atlas.", viewModel.uiState.value.player!!.feedback!!.message)
    }

    @Test
    fun `non-hosts get no timer editor`() = runTest {
        sessionTransport.participating = true
        sessionTransport.host = false
        val viewModel = connectedViewModel(seatedLobby(2))
        viewModel.signIn()

        assertNull(viewModel.uiState.value.player!!.timerEditor)
    }

    @Test
    fun `a new Atlas boot drops the session`() = runTest {
        sessionTransport.participating = true
        val viewModel = connectedViewModel(table("running.response.json", 4))
        viewModel.signIn()
        assertTrue(viewModel.uiState.value.player!!.signedIn)

        repository.tableSummary.value = table("running.response.json", 1).copy(bootId = Fixtures.OTHER_BOOT_ID)

        assertEquals(false, viewModel.uiState.value.player!!.signedIn)
    }

    @Test
    fun `repository failures are shown and editing is locked while connected`() = runTest {
        val viewModel = viewModel()

        repository.connectionState.value = AtlasConnectionState.CONNECTED
        repository.failure.value = AtlasFailure.Timeout("SocketTimeoutException: test")

        val state = viewModel.uiState.value
        assertEquals(repository.failure.value!!.userMessage, state.errorMessage)
        assertEquals("SocketTimeoutException: test", state.errorDetail)
        assertTrue(state.isRetrying)
        assertEquals(false, state.endpointEditable)
    }
}
