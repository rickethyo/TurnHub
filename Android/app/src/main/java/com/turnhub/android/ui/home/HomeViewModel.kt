package com.turnhub.android.ui.home

import com.turnhub.android.protocol.AccessibilitySettings
import com.turnhub.android.protocol.LedStyle
import androidx.lifecycle.ViewModel
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewModelScope
import androidx.lifecycle.viewmodel.initializer
import androidx.lifecycle.viewmodel.viewModelFactory
import com.turnhub.android.data.ActionFeedback
import com.turnhub.android.data.AtlasEndpoint
import com.turnhub.android.data.AtlasException
import com.turnhub.android.data.AtlasFailure
import com.turnhub.android.data.AtlasPlayerSession
import com.turnhub.android.data.AtlasRepository
import com.turnhub.android.data.AtlasRestarts
import com.turnhub.android.data.AtlasUpdateWatcher
import com.turnhub.android.data.AtlasWifiLink
import com.turnhub.android.data.ControlAction
import com.turnhub.android.data.PlayerSessionState
import com.turnhub.android.data.UpdatesState
import com.turnhub.android.data.WifiCredentialStore
import com.turnhub.android.data.WifiCredentials
import com.turnhub.android.data.WifiJoinResult
import com.turnhub.android.domain.TableSummary
import com.turnhub.android.protocol.AtlasConnectionState
import com.turnhub.android.protocol.GameSettingsInfo
import com.turnhub.android.protocol.ProfileSummary
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.flow.stateIn
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch
import kotlinx.coroutines.delay
import kotlinx.coroutines.withTimeoutOrNull
import com.turnhub.android.data.AtlasSetupAssistant
import com.turnhub.android.data.FirmwareReleaseSource
import com.turnhub.android.data.GitHubFirmwareReleases
import com.turnhub.android.data.SetupHost
import com.turnhub.android.data.SetupState

/**
 * Combines [AtlasRepository] state and the screen's own inputs into
 * [HomeUiState], and runs the Home screen's connect sequence:
 *
 * 1. (Activity) local-network permission.
 * 2. For Atlas's access-point address, join its Wi-Fi through [wifiLink] with
 *    the saved password for that SSID, or the shipped default if none is saved.
 * 3. If joining fails, ask for the Wi-Fi password (or let the user say they
 *    already joined it manually) and try again.
 * 4. Save credentials that joined successfully, then [AtlasRepository.connect].
 *
 * The repository is built from [repositoryFactory] with this ViewModel's scope,
 * so its polling lives exactly as long as the screen's ViewModel. This class
 * knows nothing about HTTP or Android networking; it depends only on interfaces.
 */
class HomeViewModel(
    repositoryFactory: (CoroutineScope) -> AtlasRepository,
    private val wifiLink: AtlasWifiLink,
    private val credentialStore: WifiCredentialStore,
    private val playerSession: AtlasPlayerSession,
    releases: FirmwareReleaseSource = GitHubFirmwareReleases(),
    /** Reports newer firmware to Atlas while connected; null in tests (no network). */
    private val updateWatcher: AtlasUpdateWatcher? = null,
) : ViewModel() {

    private val repository: AtlasRepository = repositoryFactory(viewModelScope)

    /** Device Settings, accounts and the Developer page (Atlas checks every permission). */
    private val adminConsole = com.turnhub.android.data.AtlasAdminConsole(
        playerSession,
        object : AtlasRestarts {
            override suspend fun wifiPasswordChanged(password: String) {
                val ssid = wifiLink.joinedSsid.value ?: credentialStore.lastSsid() ?: WifiCredentials.DEFAULT_ATLAS_SSID
                credentialStore.save(WifiCredentials(ssid, password))
                this@HomeViewModel.reconnectAfterRestart()
            }

            override suspend fun factoryReset() {
                credentialStore.save(
                    WifiCredentials(WifiCredentials.DEFAULT_ATLAS_SSID, WifiCredentials.DEFAULT_ATLAS_PASSPHRASE),
                )
                this@HomeViewModel.reconnectAfterRestart()
            }
        },
    )
    val adminState: StateFlow<com.turnhub.android.data.AdminState> = adminConsole.state

    /**
     * First-run setup (Documentation/engineering/FIRST_RUN_SETUP.md): shown
     * instead of the table while Atlas reports its Welcome stage. Checked on
     * every new Atlas boot.
     */
    private val setupAssistant = AtlasSetupAssistant(
        playerSession,
        releases,
        object : SetupHost {
            override fun endpoint(): AtlasEndpoint? = repository.endpoint.value
            override fun atlasFirmware(): String? = repository.tableSummary.value?.firmwareVersion
            override fun wifiPasswordChanged(ssid: String, password: String) =
                credentialStore.save(WifiCredentials(ssid, password))
            // Qualified: unqualified, this resolved to this override itself and
            // recursed until the stack overflowed (crash after Finish, 2026-09-30).
            override suspend fun reconnectAfterRestart(): Boolean = this@HomeViewModel.reconnectAfterRestart()
        },
    )
    val setupState: StateFlow<SetupState> = setupAssistant.state

    init {
        // The update step knows better than the last report: when it found
        // nothing to install, or an install ended, read Atlas's count again now
        // (the banner otherwise waits for the next refresh).
        viewModelScope.launch {
            setupAssistant.state.map { it.updates }.distinctUntilChanged().collect { updates ->
                if ((updates is UpdatesState.Ready && !updates.plan.anyUpdate) || updates is UpdatesState.Finished) {
                    updateWatcher?.requestRefresh()
                }
            }
        }
    }

    /** Everything this screen owns that the repository doesn't. */
    private data class LocalState(
        val endpointText: String = AtlasEndpoint.DEFAULT.baseUrl,
        /** Problems found before any Atlas request (bad address, permission, Wi-Fi). */
        val failure: AtlasFailure? = null,
        val wifiPrompt: WifiPrompt? = null,
        val joiningSsid: String? = null,
        val signIn: SignInPrompt? = null,
        val accessibilityOpen: Boolean = false,
        val discovery: Discovery = Discovery.Idle,
        val rejoining: Boolean = false,
    )

    private val local = MutableStateFlow(LocalState())

    /** The endpoint a pending Wi-Fi prompt will connect to once answered. */
    private var pendingEndpoint: AtlasEndpoint? = null

    private data class SessionView(
        val session: PlayerSessionState,
        val busy: Boolean,
        val feedback: ActionFeedback?,
        val gameSettings: GameSettingsInfo?,
        val accessibility: AccessibilitySettings?,
    )

    private val baseSessionFlows = combine(
        playerSession.state,
        playerSession.busy,
        playerSession.feedback,
        playerSession.gameSettings,
        playerSession.accessibility,
        ::SessionView,
    )

    private val sessionFlows = combine(
        baseSessionFlows,
        playerSession.personalization,
        playerSession.avatars,
    ) { view, personalization, avatars -> Triple(view, personalization, avatars) }

    val uiState: StateFlow<HomeUiState> = combine(
        repository.connectionState,
        repository.tableSummary,
        repository.failure,
        local,
        sessionFlows,
    ) { connectionState, tableSummary, repositoryFailure, screen, (view, personalization, avatars) ->
        val (session, busy, feedback, gameSettings, accessibility) = view
        val shown = screen.failure ?: repositoryFailure
        HomeUiState(
            connectionState = connectionState,
            endpointText = screen.endpointText,
            tableSummary = tableSummary,
            errorMessage = shown?.userMessage,
            errorDetail = shown?.technicalDetail,
            offerAppSettings = shown is AtlasFailure.LocalNetworkPermissionDenied,
            joiningSsid = screen.joiningSsid,
            wifiPrompt = screen.wifiPrompt,
            player = tableSummary?.let { PlayerPanel.from(it, session, busy, feedback, gameSettings) },
            signIn = screen.signIn,
            gameSettings = gameSettings,
            personalization = personalization,
            avatars = avatars,
            accessibility = if (screen.accessibilityOpen && session is PlayerSessionState.SignedIn) {
                AccessibilityPrompt(
                    settings = accessibility,
                    busy = busy,
                    error = feedback?.takeIf { it.isError }?.message,
                )
            } else {
                null
            },
            discovery = screen.discovery,
            hasSavedTable = credentialStore.lastSsid() != null,
            rejoining = screen.rejoining,
        )
    }.stateIn(
        scope = viewModelScope,
        started = SharingStarted.WhileSubscribed(stopTimeoutMillis = 5_000),
        initialValue = HomeUiState(),
    )

    init {
        viewModelScope.launch {
            playerSession.state.collect { if (it is PlayerSessionState.SignedOut) adminConsole.forget() }
        }
        // Give the Atlas Wi-Fi back whenever the Atlas connection ends
        // (Disconnect, lost connection, failed handshake).
        viewModelScope.launch {
            var previous = repository.connectionState.value
            repository.connectionState.collect { state ->
                if (previous != AtlasConnectionState.DISCONNECTED && state == AtlasConnectionState.DISCONNECTED) {
                    wifiLink.release()
                }
                previous = state
            }
        }
        // Atlas sessions are RAM-only: drop ours when the connection ends or
        // Atlas's identity/boot changes; re-read it when the table changes.
        viewModelScope.launch {
            var previous: TableSummary? = null
            repository.tableSummary.collect { summary ->
                val last = previous
                when {
                    summary == null || (last != null &&
                        (summary.atlasId != last.atlasId || summary.bootId != last.bootId)) -> {
                        playerSession.forget()
                        if (summary == null) local.update { it.copy(signIn = null) }
                    }
                    last != null && summary.revision != last.revision -> launch { playerSession.refresh() }
                }
                // A new Atlas (or a new boot of it): is it still being set up?
                if (summary != null && (last == null || summary.atlasId != last.atlasId || summary.bootId != last.bootId)) {
                    launch { setupAssistant.check() }
                }
                // Tell Atlas about newer firmware (it has no internet itself).
                val endpoint = repository.endpoint.value
                if (summary != null && endpoint != null && updateWatcher != null) {
                    launch { updateWatcher.onTick(endpoint, summary.bootId) }
                }
                previous = summary
            }
        }
    }

    // --- playing from this phone ------------------------------------------------

    /** Opens the sign-in picker with Atlas's profile list. */
    fun onPlayFromPhoneClicked() {
        val endpoint = repository.endpoint.value ?: return
        local.update { it.copy(signIn = SignInPrompt(loading = true)) }
        viewModelScope.launch {
            val prompt = try {
                SignInPrompt(profiles = playerSession.profiles(endpoint))
            } catch (e: AtlasException) {
                SignInPrompt(error = e.failure.userMessage)
            }
            local.update { state -> state.copy(signIn = state.signIn?.let { prompt }) }
        }
    }

    fun onSignInSubmitted(profile: ProfileSummary, pin: String) {
        val endpoint = repository.endpoint.value ?: return
        val prompt = local.value.signIn ?: return
        if (!pin.matches(PIN_PATTERN)) {
            local.update { it.copy(signIn = prompt.copy(error = "PINs are 4 to 8 digits.")) }
            return
        }
        local.update { it.copy(signIn = prompt.copy(submitting = true, error = null)) }
        viewModelScope.launch {
            try {
                playerSession.signIn(endpoint, profile, pin)
                local.update { it.copy(signIn = null) }
            } catch (e: AtlasException) {
                local.update { state ->
                    state.copy(signIn = state.signIn?.copy(submitting = false, error = e.failure.userMessage))
                }
            }
        }
    }

    fun onSignInDismissed() {
        local.update { it.copy(signIn = null) }
    }

    fun onJoinClicked() {
        viewModelScope.launch { playerSession.join() }
    }

    fun onPassClicked() = sendControl(ControlAction.PASS)

    fun onPauseResumeClicked() = sendControl(ControlAction.PAUSE_RESUME)

    /** Host only; Atlas re-validates the value and the host/lobby rule. */
    fun onTurnTimerChosen(turnTimerMs: Long) {
        viewModelScope.launch { playerSession.setTurnTimer(turnTimerMs) }
    }

    /** Opens the Sigil accessibility editor and reads the profile's current choices from Atlas. */
    fun onAccessibilityClicked() {
        playerSession.clearFeedback()
        local.update { it.copy(accessibilityOpen = true) }
        viewModelScope.launch { playerSession.loadAccessibility() }
    }

    /** Atlas validates and stores the choices; the editor closes once Atlas accepts them. */
    fun onAccessibilitySaved(sigilSound: Boolean, ledStyle: LedStyle, longPressMs: Int, winHoldMs: Int) {
        viewModelScope.launch {
            playerSession.saveAccessibility(sigilSound, ledStyle, longPressMs, winHoldMs)
            if (playerSession.feedback.value?.isError != true) local.update { it.copy(accessibilityOpen = false) }
        }
    }

    fun onAccessibilityDismissed() {
        local.update { it.copy(accessibilityOpen = false) }
    }

    /** Any game control; Atlas decides whether it applies now. */
    fun onControl(action: ControlAction) = sendControl(action)

    /** Changes this player's own life; applies immediately on Atlas. */
    fun onChangeMyLife(delta: Int) {
        if (delta == 0) return
        viewModelScope.launch {
            playerSession.counter("/api/control/life", listOf("delta" to delta.toString()), "Life updated.")
        }
    }

    /** Asks [target] to approve a change to their life (Atlas accepts it after 15 s unless rejected). */
    fun onRequestLife(target: Int, delta: Int) {
        if (delta == 0) return
        viewModelScope.launch {
            playerSession.counter(
                "/api/control/life/request",
                listOf("target" to target.toString(), "delta" to delta.toString()),
                "Life change requested.",
            )
        }
    }

    fun onRespondLife(requestId: Long, accept: Boolean) {
        viewModelScope.launch {
            playerSession.counter(
                "/api/control/life/respond",
                listOf("requestId" to requestId.toString(), "accept" to if (accept) "1" else "0"),
                if (accept) "Life change accepted." else "Life change rejected.",
            )
        }
    }

    /** Records Commander damage this player received (negative corrects it). */
    fun onCommanderDamage(source: Int, commander: Int, delta: Int) {
        if (delta == 0) return
        viewModelScope.launch {
            playerSession.counter(
                "/api/control/commander",
                listOf("source" to source.toString(), "commander" to commander.toString(), "delta" to delta.toString()),
                "Commander damage and life updated.",
            )
        }
    }

    fun onSaveGameSettings(gameProfile: String?, startingLife: Int?, turnTimerMs: Long?) {
        viewModelScope.launch { playerSession.saveGameSettings(gameProfile, startingLife, turnTimerMs) }
    }

    fun onSaveName(name: String) {
        val clean = name.trim()
        if (clean.isEmpty()) return
        viewModelScope.launch { playerSession.saveProfile(clean, null) }
    }

    fun onSavePin(pin: String) {
        if (!pin.matches(PIN_PATTERN)) {
            playerSession.clearFeedback()
            return
        }
        viewModelScope.launch { playerSession.saveProfile(null, pin) }
    }

    fun onLoadPersonalization() {
        viewModelScope.launch { playerSession.loadPersonalization() }
    }

    /** [color] is `#rrggbb` or `none`; [avatar] 0 clears it. */
    fun onSavePersonalization(color: String?, avatar: Int?) {
        viewModelScope.launch { playerSession.savePersonalization(color, avatar) }
    }

    /** Runs one admin/developer request; the outcome shows in [adminState]. */
    fun onAdmin(block: suspend com.turnhub.android.data.AtlasAdminConsole.() -> Unit) {
        viewModelScope.launch { adminConsole.block() }
    }

    fun onAdminMessageDismissed() = adminConsole.clearMessage()

    // --- first-run setup --------------------------------------------------------

    /** Runs one setup step; the outcome shows in [setupState]. */
    fun onSetup(block: suspend AtlasSetupAssistant.() -> Unit) {
        viewModelScope.launch { setupAssistant.block() }
    }

    fun onSetupDismissed() = setupAssistant.dismiss()

    /** Devices Atlas counts as behind the newest release; drives the "Update available" banner. */
    val updatesAvailable: StateFlow<Int> =
        updateWatcher?.available ?: MutableStateFlow(0)

    /** The banner's Update button: the update step on its own. */
    fun onUpdatesOpened() {
        viewModelScope.launch { setupAssistant.openUpdates() }
    }

    fun onSetupClosed() {
        updateWatcher?.requestRefresh()
        setupAssistant.close()
    }

    /**
     * Atlas restarts (an update, a new Wi-Fi password, factory reset). Waits
     * for it to go down, then rejoins its Wi-Fi with the saved password and
     * reconnects until a new boot answers. Sessions reset for the new boot.
     */
    private suspend fun reconnectAfterRestart(): Boolean {
        val endpoint = repository.endpoint.value ?: AtlasEndpoint.DEFAULT
        val oldBoot = repository.tableSummary.value?.bootId
        local.update { it.copy(rejoining = true, discovery = Discovery.Idle) }
        val back = try {
            delay(RESTART_GRACE_MS)
            repository.disconnect()
            wifiLink.release()
            withTimeoutOrNull(RECONNECT_TIMEOUT_MS) { rejoinUntilNewBoot(endpoint, oldBoot) } ?: false
        } finally {
            local.update { it.copy(rejoining = false) }
        }
        if (!back) local.update { it.copy(discovery = Discovery.NotFound) }
        // Let the session reset for the new boot run before setup signs in again.
        if (back) delay(500)
        return back
    }

    private suspend fun rejoinUntilNewBoot(endpoint: AtlasEndpoint, oldBoot: String?): Boolean {
        while (true) {
            if (endpoint == AtlasEndpoint.DEFAULT) {
                val ssid = credentialStore.lastSsid() ?: WifiCredentials.DEFAULT_ATLAS_SSID
                val credentials = credentialStore.load(ssid)
                    ?: WifiCredentials(ssid, WifiCredentials.DEFAULT_ATLAS_PASSPHRASE)
                if (wifiLink.join(credentials) == WifiJoinResult.Joined) repository.connect(endpoint)
            } else {
                repository.connect(endpoint)
            }
            val summary = repository.tableSummary.value
            if (summary != null && summary.bootId != oldBoot) return true
            repository.disconnect()
            delay(RECONNECT_RETRY_MS)
        }
    }

    // --- finding the table on launch --------------------------------------------

    private var autoStarted = false

    /**
     * The Activity calls this on launch, once permissions are settled: rejoins
     * the table this phone knows. Android remembers its approval, so this is
     * silent when the table is there. No Wi-Fi scan: on this Android, scan
     * results need location permission, which the owner chose not to ask for
     * (2026-09-30). Runs once per ViewModel, so rotating the phone doesn't
     * search again.
     */
    fun onAppStarted() {
        if (autoStarted) return
        autoStarted = true
        viewModelScope.launch { rejoinSavedTable() }
    }

    /** "Try again" after the saved table didn't answer. */
    fun onSearchAgain() {
        viewModelScope.launch { rejoinSavedTable() }
    }

    /**
     * "Set up a new table": a new or factory-reset Atlas uses the printed
     * password, and Android's own dialog lists it; setup opens once it answers.
     */
    fun onSetUpNewTable() {
        local.update { it.copy(discovery = Discovery.Idle, endpointText = AtlasEndpoint.DEFAULT.baseUrl, failure = null) }
        joinThenConnect(
            AtlasEndpoint.DEFAULT,
            WifiCredentials(WifiCredentials.DEFAULT_ATLAS_SSID, WifiCredentials.DEFAULT_ATLAS_PASSPHRASE),
        )
    }

    /** Nothing else is joining, connected, rejoining or asking. */
    private fun idle(): Boolean {
        val screen = local.value
        return repository.connectionState.value == AtlasConnectionState.DISCONNECTED &&
            screen.joiningSsid == null && screen.wifiPrompt == null && !screen.rejoining
    }

    private suspend fun rejoinSavedTable() {
        if (!idle()) return
        val saved = credentialStore.lastSsid()?.let(credentialStore::load)
        if (saved == null) {
            // A phone that has never joined a table: the screen offers setup.
            local.update { it.copy(discovery = Discovery.Idle) }
            return
        }
        local.update { it.copy(discovery = Discovery.Searching, failure = null) }
        val joined = quietJoin(AtlasEndpoint.DEFAULT, saved)
        local.update { it.copy(discovery = if (joined) Discovery.Idle else Discovery.NotFound) }
    }

    /** Joins and connects; false (and no prompt) if Android couldn't join. */
    private suspend fun quietJoin(endpoint: AtlasEndpoint, credentials: WifiCredentials): Boolean {
        local.update { it.copy(joiningSsid = credentials.ssid, failure = null) }
        val result = wifiLink.join(credentials)
        local.update { it.copy(joiningSsid = null) }
        if (result != WifiJoinResult.Joined) return false
        credentialStore.save(credentials)
        connectRepository(endpoint)
        return true
    }
    /** Reads what the signed-in account may administer. */
    fun onAdminRefresh() {
        val info = (playerSession.state.value as? PlayerSessionState.SignedIn)?.info ?: return
        viewModelScope.launch {
            adminConsole.refresh(
                admin = info.has(com.turnhub.android.protocol.AccountPermission.ADMIN),
                gameMaster = info.has(com.turnhub.android.protocol.AccountPermission.GAME_MASTER),
            )
        }
    }

    fun onDeveloperRefresh() {
        viewModelScope.launch { adminConsole.refreshDeveloper() }
    }

    /** Downloads the serial log; [onText] shares it (the Activity owns sharing). */
    fun onDownloadLog(onText: (String) -> Unit) {
        viewModelScope.launch { adminConsole.downloadLog()?.let(onText) }
    }

    fun onSignOutClicked() {
        viewModelScope.launch { playerSession.signOut() }
    }

    fun onFeedbackDismissed() = playerSession.clearFeedback()

    /** Sends once, pinned to the snapshot the player was looking at. */
    private fun sendControl(action: ControlAction) {
        val summary = repository.tableSummary.value ?: return
        viewModelScope.launch { playerSession.control(action, summary.revision, summary.bootId) }
    }

    fun onEndpointChanged(text: String) {
        local.update { it.copy(endpointText = text, failure = null) }
    }

    /**
     * Starts connecting to the entered endpoint. The Activity calls this only
     * once the platform's local-network permission is granted (or not required).
     */
    fun onConnectClicked() {
        if (local.value.joiningSsid != null) return
        val endpoint = AtlasEndpoint.parse(local.value.endpointText).getOrElse { error ->
            local.update {
                it.copy(failure = (error as? AtlasException)?.failure ?: AtlasFailure.Unexpected(error.message))
            }
            return
        }
        local.update {
            it.copy(endpointText = endpoint.baseUrl, failure = null, wifiPrompt = null, discovery = Discovery.Idle)
        }
        if (endpoint != AtlasEndpoint.DEFAULT) {
            // Not Atlas's own access point (e.g. a future Home/LAN address):
            // the phone must already be on that network.
            connectRepository(endpoint)
            return
        }
        val ssid = credentialStore.lastSsid() ?: WifiCredentials.DEFAULT_ATLAS_SSID
        val credentials = credentialStore.load(ssid)
            ?: WifiCredentials(ssid, WifiCredentials.DEFAULT_ATLAS_PASSPHRASE).takeIf {
                ssid == WifiCredentials.DEFAULT_ATLAS_SSID
            }
        if (credentials == null) {
            askForPassword(endpoint, ssid, "Enter the Wi-Fi password for $ssid.")
        } else {
            joinThenConnect(endpoint, credentials)
        }
    }

    /** The user entered Atlas Wi-Fi details in the prompt. */
    fun onWifiPasswordSubmitted(ssid: String, passphrase: String) {
        val endpoint = pendingEndpoint ?: AtlasEndpoint.DEFAULT
        val name = ssid.trim()
        when {
            name.isEmpty() -> askForPassword(endpoint, name, "Enter the Atlas Wi-Fi network name.")
            passphrase.length !in WifiCredentials.PASSPHRASE_LENGTH ->
                askForPassword(endpoint, name, "Wi-Fi passwords are 8 to 63 characters.")
            else -> {
                local.update { it.copy(wifiPrompt = null) }
                joinThenConnect(endpoint, WifiCredentials(name, passphrase))
            }
        }
    }

    /** The user already joined the Atlas Wi-Fi in Android settings. */
    fun onUseCurrentWifi() {
        val endpoint = pendingEndpoint ?: AtlasEndpoint.DEFAULT
        local.update { it.copy(wifiPrompt = null) }
        connectRepository(endpoint)
    }

    fun onWifiPromptDismissed() {
        pendingEndpoint = null
        local.update { it.copy(wifiPrompt = null) }
    }

    fun onDisconnectClicked() {
        local.update { it.copy(discovery = Discovery.Idle) }
        viewModelScope.launch {
            repository.disconnect()
            wifiLink.release()
        }
    }

    /** The user declined Android 17's local-network ("Nearby devices") permission. */
    fun onLocalNetworkPermissionDenied() {
        local.update { it.copy(failure = AtlasFailure.LocalNetworkPermissionDenied) }
    }

    override fun onCleared() {
        wifiLink.release()
    }

    private fun joinThenConnect(endpoint: AtlasEndpoint, credentials: WifiCredentials) {
        local.update { it.copy(joiningSsid = credentials.ssid, failure = null) }
        viewModelScope.launch {
            val result = wifiLink.join(credentials)
            local.update { it.copy(joiningSsid = null) }
            when (result) {
                WifiJoinResult.Joined -> {
                    credentialStore.save(credentials)
                    connectRepository(endpoint)
                }
                WifiJoinResult.Unavailable -> askForPassword(
                    endpoint,
                    credentials.ssid,
                    "Couldn't join ${credentials.ssid}. Check the password and that Atlas is on and " +
                        "nearby. If Android asked to connect, choose Connect.",
                )
                is WifiJoinResult.Failed -> askForPassword(
                    endpoint,
                    credentials.ssid,
                    "Couldn't join ${credentials.ssid}" + (result.detail?.let { ": $it" } ?: "."),
                )
            }
        }
    }

    private fun askForPassword(endpoint: AtlasEndpoint, ssid: String, message: String) {
        pendingEndpoint = endpoint
        local.update { it.copy(wifiPrompt = WifiPrompt(ssid = ssid, message = message)) }
    }

    private fun connectRepository(endpoint: AtlasEndpoint) {
        pendingEndpoint = null
        viewModelScope.launch { repository.connect(endpoint) }
    }

    companion object {
        private val PIN_PATTERN = Regex("^\\d{4,8}$")

        /** Atlas answers, then restarts about a second later. */
        private const val RESTART_GRACE_MS = 4_000L
        private const val RECONNECT_RETRY_MS = 4_000L

        /** An update boot plus the Wi-Fi coming back; well past a normal restart. */
        private const val RECONNECT_TIMEOUT_MS = 150_000L

        /**
         * Minimal manual-DI factory: no framework is introduced for one ViewModel.
         * Revisit if/when the dependency graph actually grows past this.
         */
        fun factory(
            repositoryFactory: (CoroutineScope) -> AtlasRepository,
            wifiLink: AtlasWifiLink,
            credentialStore: WifiCredentialStore,
            playerSession: AtlasPlayerSession,
            updateCheckIntervalMs: Long = AtlasUpdateWatcher.RELEASE_INTERVAL_MS,
        ): ViewModelProvider.Factory = viewModelFactory {
            initializer {
                val releases = GitHubFirmwareReleases()
                HomeViewModel(
                    repositoryFactory, wifiLink, credentialStore, playerSession, releases,
                    AtlasUpdateWatcher(playerSession, releases, updateCheckIntervalMs),
                )
            }
        }
    }
}
