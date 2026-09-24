package com.turnhub.android.ui.home

import androidx.lifecycle.ViewModel
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewModelScope
import androidx.lifecycle.viewmodel.initializer
import androidx.lifecycle.viewmodel.viewModelFactory
import com.turnhub.android.data.AtlasEndpoint
import com.turnhub.android.data.AtlasException
import com.turnhub.android.data.AtlasFailure
import com.turnhub.android.data.AtlasPlayerSession
import com.turnhub.android.data.AtlasRepository
import com.turnhub.android.data.AtlasWifiLink
import com.turnhub.android.data.ControlAction
import com.turnhub.android.data.PlayerSessionState
import com.turnhub.android.data.WifiCredentialStore
import com.turnhub.android.data.WifiCredentials
import com.turnhub.android.data.WifiJoinResult
import com.turnhub.android.domain.TableSummary
import com.turnhub.android.protocol.AtlasConnectionState
import com.turnhub.android.protocol.ProfileSummary
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.stateIn
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch

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
) : ViewModel() {

    private val repository: AtlasRepository = repositoryFactory(viewModelScope)

    /** Everything this screen owns that the repository doesn't. */
    private data class LocalState(
        val endpointText: String = AtlasEndpoint.DEFAULT.baseUrl,
        /** Problems found before any Atlas request (bad address, permission, Wi-Fi). */
        val failure: AtlasFailure? = null,
        val wifiPrompt: WifiPrompt? = null,
        val joiningSsid: String? = null,
        val signIn: SignInPrompt? = null,
    )

    private val local = MutableStateFlow(LocalState())

    /** The endpoint a pending Wi-Fi prompt will connect to once answered. */
    private var pendingEndpoint: AtlasEndpoint? = null

    private val sessionFlows = combine(playerSession.state, playerSession.busy, playerSession.feedback, ::Triple)

    val uiState: StateFlow<HomeUiState> = combine(
        repository.connectionState,
        repository.tableSummary,
        repository.failure,
        local,
        sessionFlows,
    ) { connectionState, tableSummary, repositoryFailure, screen, (session, busy, feedback) ->
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
            player = tableSummary?.let { PlayerPanel.from(it, session, busy, feedback) },
            signIn = screen.signIn,
        )
    }.stateIn(
        scope = viewModelScope,
        started = SharingStarted.WhileSubscribed(stopTimeoutMillis = 5_000),
        initialValue = HomeUiState(),
    )

    init {
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
        local.update { it.copy(endpointText = endpoint.baseUrl, failure = null, wifiPrompt = null) }
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

        /**
         * Minimal manual-DI factory: no framework is introduced for one ViewModel.
         * Revisit if/when the dependency graph actually grows past this.
         */
        fun factory(
            repositoryFactory: (CoroutineScope) -> AtlasRepository,
            wifiLink: AtlasWifiLink,
            credentialStore: WifiCredentialStore,
            playerSession: AtlasPlayerSession,
        ): ViewModelProvider.Factory = viewModelFactory {
            initializer { HomeViewModel(repositoryFactory, wifiLink, credentialStore, playerSession) }
        }
    }
}
