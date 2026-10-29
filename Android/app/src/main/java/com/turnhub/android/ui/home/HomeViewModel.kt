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
import com.turnhub.android.data.ProfileSecret
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
import kotlinx.coroutines.Job
import kotlinx.coroutines.currentCoroutineContext
import kotlinx.coroutines.ensureActive
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.collectLatest
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
    /** App lock and automatic sign-in; null where the phone can't keep a profile (and in tests). */
    private val vault: com.turnhub.android.data.ProfileVault? = null,
) : ViewModel() {

    private val repository: AtlasRepository = repositoryFactory(viewModelScope)

    /**
     * Local game/library owner. Atlas delivery requires an explicit per-match
     * mapping; connected profiles never become local identity by name.
     */
    private var standaloneObserver: Job? = null
    var standalone: com.turnhub.android.standalone.StandaloneTable? = null
        set(value) {
            field = value
            standaloneObserver?.cancel()
            if (value != null) standaloneObserver = viewModelScope.launch {
                combine(value.state.map { it.library.history.map { r -> r.recordId } to it.library.players.map { p -> p.links } }.distinctUntilChanged(),
                    repository.tableSummary.map { it?.atlasId to it?.bootId }.distinctUntilChanged(),
                    playerSession.state.map { it is PlayerSessionState.SignedIn }.distinctUntilChanged(),
                    repository.offlineSinceMs.map { it == null }.distinctUntilChanged()) { _, epoch, signedIn, online -> Triple(epoch, signedIn, online) }
                    .collect { (_, signedIn, online) -> if (signedIn && online) onLoadLocalImportProfiles() }
            }
        }
    private val _localImport = MutableStateFlow(com.turnhub.android.standalone.LocalImportState())
    val localImport: StateFlow<com.turnhub.android.standalone.LocalImportState> = _localImport.asStateFlow()
    private var localImportJob: Job? = null

    private fun supportsExplicitLocalImport(version: String): Boolean =
        runCatching { com.turnhub.android.data.FirmwareVersion.parse(version) }.getOrNull()?.let {
            it >= com.turnhub.android.data.FirmwareVersion(0, 7, 2)
        } == true

    fun onLoadLocalImportProfiles() {
        val summary = repository.tableSummary.value ?: return
        val endpoint = repository.endpoint.value ?: return
        if (repository.connectionState.value != AtlasConnectionState.CONNECTED ||
            localImportJob?.isActive == true) return
        if (!supportsExplicitLocalImport(summary.firmwareVersion)) {
            _localImport.value = com.turnhub.android.standalone.LocalImportState(message = "Update Atlas to 0.7.2 or later before importing. Older firmware can match players by name.")
            return
        }
        _localImport.value = com.turnhub.android.standalone.LocalImportState(atlasId = summary.atlasId, busy = true)
        localImportJob = viewModelScope.launch {
            try {
                val profiles = playerSession.profiles(endpoint)
                if (repository.tableSummary.value?.atlasId == summary.atlasId) {
                    _localImport.value = com.turnhub.android.standalone.LocalImportState(summary.atlasId, profiles)
                    val signed = playerSession.state.value as? PlayerSessionState.SignedIn
                    if (signed != null) standalone?.state?.value?.library?.players
                        ?.firstOrNull { it.profileId == signed.profileId && it.links.none { l -> l.atlasId == summary.atlasId } }
                        ?.let { standalone?.linkAccount(it.localId, summary.atlasId, signed.profileId) }
                    syncLinkedProfiles(summary, endpoint)
                    if (playerSession.state.value is PlayerSessionState.SignedIn) sendAutomaticLocalRecords(summary.atlasId, profiles)
                } else _localImport.value = com.turnhub.android.standalone.LocalImportState(message = "Atlas changed. Load its players again.")
            } catch (e: AtlasException) {
                _localImport.value = com.turnhub.android.standalone.LocalImportState(message = e.failure.userMessage)
            }
        }
    }

    fun onImportLocalRecord(recordId: String, profileIds: List<String>) {
        val summary = repository.tableSummary.value ?: return
        val choices = _localImport.value
        val table = standalone ?: return
        if (!supportsExplicitLocalImport(summary.firmwareVersion) || choices.atlasId != summary.atlasId || localImportJob?.isActive == true ||
            repository.connectionState.value != AtlasConnectionState.CONNECTED ||
            playerSession.state.value !is PlayerSessionState.SignedIn) return
        if (!table.queueImport(recordId, summary.atlasId, profileIds, choices.profiles.map { it.profileId }.toSet(),
                supportsGuestImport(summary.firmwareVersion))) {
            _localImport.value = choices.copy(message = "Choose a different Atlas profile for every player. A pending or imported mapping cannot be changed.")
            return
        }
        _localImport.value = choices.copy(busy = true, message = null)
        localImportJob = viewModelScope.launch {
            try {
                table.sendRecords(summary.atlasId) { fields ->
                    if (repository.tableSummary.value?.atlasId != summary.atlasId ||
                        repository.connectionState.value != AtlasConnectionState.CONNECTED) null
                    else try { playerSession.raw("POST", "/api/standalone/import", fields) }
                    catch (_: AtlasException) { null }
                }
            } finally {
                _localImport.value = _localImport.value.copy(busy = false)
            }
        }
    }

    fun onLinkLocalAccount(localId: String, targetId: String?, secret: String) {
        val summary = repository.tableSummary.value ?: return
        val endpoint = repository.endpoint.value ?: return
        val table = standalone ?: return
        val player = table.state.value.library.players.firstOrNull { it.localId == localId } ?: return
        if (!table.accountUnlocked(localId)) return
        linkLocalAccount(summary, endpoint, table, player, targetId, secret)
    }

    /**
     * "Bring players to this table": puts each local account that isn't on
     * this Atlas yet there under its own ID and links it, then sends their
     * games. PIN-less accounts go together on the signed-in session (Atlas
     * 0.7.18+, owner decision 2026-10-28: their first sign-in sets a PIN). An
     * account with a PIN is brought one at a time with that PIN ([secret]).
     */
    fun onBringLocalAccounts(localId: String? = null, secret: String? = null) {
        val summary = repository.tableSummary.value ?: return
        val endpoint = repository.endpoint.value ?: return
        val table = standalone ?: return
        val choices = _localImport.value
        if (choices.atlasId != summary.atlasId) return
        val players = table.state.value.library.players
        if (localId != null && secret != null) {
            val player = players.firstOrNull { it.localId == localId } ?: return
            if (player.credential == null) return
            linkLocalAccount(summary, endpoint, table, player, null, secret)
            return
        }
        if (localImportJob?.isActive == true || repository.connectionState.value != AtlasConnectionState.CONNECTED) return
        if (!supportsPinlessUpload(summary.firmwareVersion)) {
            _localImport.value = choices.copy(message = "Uploading players without a PIN needs Atlas 0.7.18 or later.")
            return
        }
        if (playerSession.state.value !is PlayerSessionState.SignedIn) {
            _localImport.value = choices.copy(message = "Sign in to this Atlas first, then bring your players.")
            return
        }
        val onAtlas = choices.profiles.map { it.profileId }.toSet()
        val waiting = players.filter { p -> p.credential == null && p.links.none { it.atlasId == summary.atlasId } && p.profileId !in onAtlas }
        if (waiting.isEmpty()) return
        _localImport.value = choices.copy(busy = true, message = null)
        localImportJob = viewModelScope.launch {
            var brought = 0
            val problems = mutableListOf<String>()
            try {
                for (player in waiting) {
                    if (repository.tableSummary.value?.atlasId != summary.atlasId ||
                        repository.connectionState.value != AtlasConnectionState.CONNECTED) break
                    val response = try { playerSession.raw("POST", "/api/profiles/register",
                        listOf("name" to player.name, "pin" to "", "profileId" to player.profileId)) } catch (_: AtlasException) { null }
                    when {
                        response?.ok == true && table.linkAccount(player.localId, summary.atlasId, player.profileId,
                            runCatching { org.json.JSONObject(response.body).optString("linkKey") }.getOrNull()
                                ?.takeIf { it.matches(Regex("[0-9A-F]{32}")) }) -> brought++
                        response?.code == 409 -> problems += "${player.name}'s ID is already on this Atlas; open Edit to link it with that account's PIN"
                        else -> problems += "${player.name} could not be uploaded"
                    }
                }
                val profiles = playerSession.profiles(endpoint)
                _localImport.value = com.turnhub.android.standalone.LocalImportState(summary.atlasId, profiles, busy = true,
                    message = (listOf("$brought player${if (brought == 1) "" else "s"} brought to this table; their games are on the way") + problems)
                        .joinToString(". ") + ".")
                syncLinkedProfiles(summary, endpoint)
                if (playerSession.state.value is PlayerSessionState.SignedIn) sendAutomaticLocalRecords(summary.atlasId, profiles)
            } catch (e: kotlinx.coroutines.CancellationException) { throw e }
            catch (e: Exception) {
                _localImport.value = _localImport.value.copy(message = (e as? AtlasException)?.failure?.userMessage ?: "Upload stopped. Local data is retained.")
            } finally { _localImport.value = _localImport.value.copy(busy = false) }
        }
    }

    private fun linkLocalAccount(summary: TableSummary, endpoint: AtlasEndpoint, table: com.turnhub.android.standalone.StandaloneTable,
                                 player: com.turnhub.android.standalone.SavedLocalPlayer, targetId: String?, secret: String) {
        val localId = player.localId
        if (localImportJob?.isActive == true || repository.connectionState.value != AtlasConnectionState.CONNECTED) return
        if (targetId == null && !supportsLocalAccountIds(summary.firmwareVersion)) {
            _localImport.value = _localImport.value.copy(message = "Account import needs Atlas 0.7.15 or later. You can still link an existing account by signing in.")
            return
        }
        _localImport.value = _localImport.value.copy(busy = true, message = null)
        localImportJob = viewModelScope.launch {
            try {
                val result = playerSession.verifyLocalAccount(endpoint, player, targetId, secret)
                if (repository.tableSummary.value?.atlasId != summary.atlasId ||
                    repository.endpoint.value != endpoint) return@launch
                if (!table.linkAccount(localId, summary.atlasId, result.profileId, result.linkKey)) {
                    _localImport.value = _localImport.value.copy(message = "Could not save this link, or another local account already uses it.")
                    return@launch
                }
                if (playerSession.state.value !is PlayerSessionState.SignedIn) {
                    // The user just supplied this account's credentials. If there
                    // is no active login, use it for automatic match delivery.
                    playerSession.signIn(endpoint, ProfileSummary(result.profileId, player.name, true), secret)
                }
                val profiles = playerSession.profiles(endpoint)
                _localImport.value = com.turnhub.android.standalone.LocalImportState(summary.atlasId, profiles,
                    busy = true, message = result.warning ?: "Account linked. Matching games will import automatically when signed in.")
                syncLinkedProfiles(summary, endpoint)
                if (playerSession.state.value is PlayerSessionState.SignedIn) sendAutomaticLocalRecords(summary.atlasId, profiles)
            } catch (e: kotlinx.coroutines.CancellationException) { throw e }
            catch (e: Exception) {
                _localImport.value = _localImport.value.copy(message = (e as? AtlasException)?.failure?.userMessage ?: "Account linking failed. Local data is retained.")
            } finally { _localImport.value = _localImport.value.copy(busy = false) }
        }
    }

    /**
     * Automatic profile sync (plans: profile-transfer F): every local account
     * with a link key for this Atlas sends its copy; the newer one wins as a
     * whole (name, color, avatar, reduced motion). Runs whenever the Atlas
     * accounts load; also collects each account's totals there. Statistics
     * themselves travel as matches, never through this.
     */
    private suspend fun syncLinkedProfiles(summary: TableSummary, endpoint: AtlasEndpoint) {
        val table = standalone ?: return
        if (!supportsPinlessUpload(summary.firmwareVersion)) return
        val linked = table.state.value.library.players.mapNotNull { p ->
            p.links.firstOrNull { it.atlasId == summary.atlasId && it.linkKey != null }?.let { p to it } }
        if (linked.isEmpty()) return
        val presets = try { playerSession.avatarPresets(endpoint) } catch (_: AtlasException) { return }
        val totals = mutableMapOf<String, Pair<Int, Int>>()
        for ((player, link) in linked) {
            if (repository.tableSummary.value?.atlasId != summary.atlasId) return
            val look = player.personalization
            val fields = buildList {
                add("profileId" to link.profileId); add("linkKey" to link.linkKey!!)
                add("now" to "${System.currentTimeMillis()}"); 
                // Never edited here still beats an Atlas copy that was never edited (0),
                // so a fresh upload's look reaches Atlas.
                add("activity" to "${maxOf(player.lastActivityMs, 1L)}")
                add("name" to player.name); add("color" to (look.color ?: "none"))
                // An uploaded image stays on the phone; the avatar only syncs a preset.
                if (look.image == null) add("avatar" to "${presets.firstOrNull { it.key == look.avatar }?.id ?: 0}")
                add("reduceMotion" to if (look.reduceMotion) "1" else "0")
            }
            val response = try { playerSession.syncProfile(endpoint, fields) } catch (_: AtlasException) { continue }
            if (response.code == 401) { table.dropLinkKey(player.localId, summary.atlasId); continue }
            if (!response.ok) continue
            val atlas = runCatching { org.json.JSONObject(response.body) }.getOrNull() ?: continue
            totals[player.localId] = atlas.optInt("gamesPlayed") to atlas.optInt("gamesWon")
            if (atlas.optString("applied") == "atlas") {
                val avatarId = atlas.optInt("avatar")
                val avatar = if (avatarId == 0) "none" else presets.firstOrNull { it.id == avatarId }?.key
                table.applySyncedProfile(player.localId, atlas.optString("name"),
                    atlas.optString("color").takeIf { it.startsWith("#") }, avatar,
                    atlas.optBoolean("reduceMotion"), atlas.optLong("activity"))
            }
        }
        if (_localImport.value.atlasId == summary.atlasId) _localImport.value = _localImport.value.copy(atlasTotals = totals)
    }

    private suspend fun sendAutomaticLocalRecords(atlasId: String, profiles: List<ProfileSummary>) {
        val table = standalone ?: return
        val guests = repository.tableSummary.value?.firmwareVersion?.let(::supportsGuestImport) == true
        table.queueAutomaticImports(atlasId, profiles.map { it.profileId }.toSet(), guests)
        table.sendRecords(atlasId) { fields ->
            if (repository.tableSummary.value?.atlasId != atlasId || repository.connectionState.value != AtlasConnectionState.CONNECTED ||
                playerSession.state.value !is PlayerSessionState.SignedIn) null
            else try { playerSession.raw("POST", "/api/standalone/import", fields) } catch (_: AtlasException) { null }
        }
    }

    /** Atlas 0.7.16+ takes matches with unlinked (guest) players, crediting each linked player once. */
    private fun supportsGuestImport(version: String): Boolean {
        val parts = version.substringBefore('-').split('.').map { it.toIntOrNull() ?: return false }
        return parts.size == 3 && (parts[0] > 0 || parts[1] > 7 || (parts[1] == 7 && parts[2] >= 16))
    }

    /** Atlas 0.7.18+ takes a PIN-less local account from a signed-in phone. */
    private fun supportsPinlessUpload(version: String): Boolean {
        val parts = version.substringBefore('-').split('.').map { it.toIntOrNull() ?: return false }
        return parts.size == 3 && (parts[0] > 0 || parts[1] > 7 || (parts[1] == 7 && parts[2] >= 18))
    }

    private fun supportsLocalAccountIds(version: String): Boolean {
        val parts = version.substringBefore('-').split('.').map { it.toIntOrNull() ?: return false }
        return parts.size == 3 && (parts[0] > 0 || parts[1] > 7 || (parts[1] == 7 && parts[2] >= 15))
    }

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

    /** This device as the table's shared screen (protocol/http-v1.md "Tablet mode"). */
    private val tablet = com.turnhub.android.data.AtlasTablet(playerSession) { repository.endpoint.value }
    val tabletState: StateFlow<com.turnhub.android.data.TabletState> = tablet.state

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
            override fun holdConnection(hold: Boolean) = repository.holdThroughOutages(hold)
            override fun deviceAdmin() = standalone?.state?.value?.library?.let { library ->
                library.players.firstOrNull { library.isDeviceAdmin(it.localId) && it.credential != null } }
            override fun deviceAdminInstalled(localId: String, profileId: String, linkKey: String?) {
                val atlasId = repository.tableSummary.value?.atlasId ?: return
                standalone?.linkAccount(localId, atlasId, profileId, linkKey)
            }
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
        val wifiOff: Boolean = false,
        val joiningSsid: String? = null,
        val signIn: SignInPrompt? = null,
        val accessibilityOpen: Boolean = false,
        val discovery: Discovery = Discovery.Idle,
        val rejoining: Boolean = false,
        /** Bumped when the vault changes, so the saved profile is re-read. */
        val vaultVersion: Int = 0,
    )

    private val local = MutableStateFlow(LocalState())

    /** What to try again once the user turns Wi-Fi on. */
    private var afterWifiOn: (() -> Unit)? = null

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

    private data class SessionExtras(
        val view: SessionView,
        val personalization: com.turnhub.android.data.Personalization?,
        val avatars: List<com.turnhub.android.protocol.AvatarIcon>,
        val choices: com.turnhub.android.data.ProfileChoices?,
        val stats: com.turnhub.android.data.StatisticsLoad?,
    )

    private val sessionFlows = combine(
        baseSessionFlows,
        combine(playerSession.personalization, playerSession.uploadProgress) { p, progress -> p?.copy(uploadProgress = progress) },
        playerSession.avatars,
        playerSession.choices,
        playerSession.stats,
        ::SessionExtras,
    )

    private val linkFlows = combine(repository.connectionState, repository.offlineSinceMs, ::Pair)

    val uiState: StateFlow<HomeUiState> = combine(
        linkFlows,
        repository.tableSummary,
        repository.failure,
        local,
        sessionFlows,
    ) { (connectionState, offlineSinceMs), tableSummary, repositoryFailure, screen, (view, personalization, avatars, choices, stats) ->
        val (session, busy, feedback, gameSettings, accessibility) = view
        val shown = screen.failure ?: repositoryFailure
        HomeUiState(
            connectionState = connectionState,
            offlineSinceMs = offlineSinceMs,
            endpointText = screen.endpointText,
            tableSummary = tableSummary,
            errorMessage = shown?.userMessage,
            errorDetail = shown?.technicalDetail,
            offerAppSettings = shown is AtlasFailure.LocalNetworkPermissionDenied,
            joiningSsid = screen.joiningSsid,
            wifiPrompt = screen.wifiPrompt,
            wifiOff = screen.wifiOff,
            player = tableSummary?.let { PlayerPanel.from(it, session, busy, feedback, gameSettings) },
            signIn = screen.signIn,
            gameSettings = gameSettings,
            personalization = personalization,
            avatars = avatars,
            profileChoices = choices,
            profileStats = stats,
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
            // vaultVersion is part of `screen`, so a save or forget recomputes this.
            savedProfile = tableSummary?.atlasId?.let { id -> vault?.saved(id) },
            appLockAvailable = vault?.available == true,
        )
    }.stateIn(
        scope = viewModelScope,
        started = SharingStarted.WhileSubscribed(stopTimeoutMillis = 5_000),
        initialValue = HomeUiState(),
    )

    init {
        viewModelScope.launch {
            playerSession.state.map { it is PlayerSessionState.SignedIn }.distinctUntilChanged().collect { signedIn ->
                if (signedIn) {
                    adminConsole.refreshDevices()
                    onLoadLocalImportProfiles()
                }
                if (!signedIn) {
                    adminConsole.forget()
                    tablet.forget()
                }
            }
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
        // Tablet changes made while Atlas wasn't answering go to it as soon as
        // it answers again; sent ones are forgotten once a snapshot carries them.
        viewModelScope.launch {
            var wasOffline = false
            repository.offlineSinceMs.collect { since ->
                val summary = repository.tableSummary.value
                if (wasOffline && since == null && summary != null) {
                    tablet.replayOffline(summary, com.turnhub.android.domain.TableClock.nowMs())
                }
                wasOffline = since != null
            }
        }
        viewModelScope.launch {
            repository.tableSummary.collect { summary -> summary?.let { tablet.settleOffline(it.revision) } }
        }
        // Profile choices are ephemeral and belong to exactly one Atlas/session.
        viewModelScope.launch {
            combine(repository.tableSummary.map { it?.atlasId }, playerSession.state.map { it is PlayerSessionState.SignedIn })
                { atlasId, signedIn -> atlasId to signedIn }
                .distinctUntilChanged().collect { (atlasId, signedIn) ->
                    if (!signedIn || atlasId != _localImport.value.atlasId) {
                        localImportJob?.cancel()
                        _localImport.value = com.turnhub.android.standalone.LocalImportState()
                    }
                }
        }
        // While Atlas isn't answering, rejoin its Wi-Fi whenever Android has
        // dropped it, so polling reaches Atlas as soon as it is back.
        viewModelScope.launch {
            repository.offlineSinceMs.collectLatest { since ->
                if (since == null) return@collectLatest
                while (true) {
                    delay(RECONNECT_RETRY_MS)
                    if (repository.endpoint.value != AtlasEndpoint.DEFAULT) continue
                    if (wifiLink.joinedSsid.value != null || local.value.rejoining) continue
                    val credentials = credentialStore.lastSsid()?.let(credentialStore::load) ?: continue
                    wifiLink.join(credentials)
                }
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
                    last != null && (summary.revision != last.revision || summary.game != last.game) -> {
                        if (summary.game != last.game) tablet.forget()
                        launch { playerSession.refresh(refreshSettings = summary.settings != last.settings || summary.game != last.game) }
                    }
                }
                // A new Atlas (or a new boot of it): is it still being set up?
                if (summary != null && (last == null || summary.atlasId != last.atlasId || summary.bootId != last.bootId)) {
                    launch { setupAssistant.check() }
                    if (!automaticProbe) offerAutomaticSignIn(summary.atlasId)
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

    fun onFollowGame(game: Int) {
        if (repository.offlineSinceMs.value != null) return
        viewModelScope.launch {
            if (playerSession.followGame(game)) tablet.forget()
        }
    }

    fun onPlayersRefresh() = viewModelScope.launch { adminConsole.refreshDevices() }

    // --- playing from this phone ------------------------------------------------

    /** The Link phone seat claim in progress or just finished. */
    val seatClaim: StateFlow<com.turnhub.android.data.SeatClaim?> = playerSession.claim

    /** "Use this seat": the player confirms with Link phone on that Sigil. */
    fun onClaimSeat(moduleId: Int, slot: Int, seatName: String) {
        val endpoint = repository.endpoint.value ?: return
        viewModelScope.launch { playerSession.claimSeat(endpoint, moduleId, slot, seatName) }
    }

    fun onSeatClaimDismissed() {
        playerSession.clearClaim()
    }

    /** Opens the sign-in picker with Atlas's profile list. */
    fun onPlayFromPhoneClicked() {
        val endpoint = repository.endpoint.value ?: return
        val offer = vault?.available == true
        local.update { it.copy(signIn = SignInPrompt(loading = true, offerRemember = offer)) }
        viewModelScope.launch {
            val prompt = try {
                SignInPrompt(profiles = playerSession.profiles(endpoint), offerRemember = offer)
            } catch (e: AtlasException) {
                SignInPrompt(error = e.failure.userMessage, offerRemember = offer)
            }
            local.update { state -> state.copy(signIn = state.signIn?.let { prompt }) }
        }
    }

    fun onSignInSubmitted(profile: ProfileSummary, pin: String, remember: Boolean = false) {
        val endpoint = repository.endpoint.value ?: return
        val prompt = local.value.signIn ?: return
        if (!ProfileSecret.isValid(pin)) {
            local.update { it.copy(signIn = prompt.copy(error = ProfileSecret.RULE)) }
            return
        }
        local.update { it.copy(signIn = prompt.copy(submitting = true, error = null)) }
        viewModelScope.launch {
            try {
                playerSession.signIn(endpoint, profile, pin)
                local.update { it.copy(signIn = null) }
                // Atlas accepted it: now (and only now) offer to keep it behind the phone's lock.
                val atlasId = repository.tableSummary.value?.atlasId
                if (remember && atlasId != null && vault?.available == true) {
                    _appLock.value = AppLockRequest.Save(
                        com.turnhub.android.data.SavedProfile(atlasId, profile.profileId, profile.name),
                        pin,
                    )
                }
            } catch (e: AtlasException) {
                local.update { state ->
                    state.copy(signIn = state.signIn?.copy(submitting = false, error = e.failure.userMessage))
                }
            }
        }
    }

    /** "Create account" in the sign-in sheet: open to anyone at the table, as in the portal. */
    fun onCreateAccountSubmitted(name: String, pin: String, remember: Boolean = false) {
        val endpoint = repository.endpoint.value ?: return
        val prompt = local.value.signIn ?: return
        val clean = name.trim()
        val problem = when {
            clean.isEmpty() || clean.length > ProfileSecret.MAX_NAME_CHARS -> "Choose a name of 1 to 32 characters."
            !ProfileSecret.isValid(pin) -> ProfileSecret.RULE
            else -> null
        }
        if (problem != null) {
            local.update { it.copy(signIn = prompt.copy(error = problem)) }
            return
        }
        local.update { it.copy(signIn = prompt.copy(submitting = true, error = null)) }
        viewModelScope.launch {
            try {
                playerSession.register(endpoint, clean, pin)
                local.update { it.copy(signIn = null) }
                val atlasId = repository.tableSummary.value?.atlasId
                val profileId = (playerSession.state.value as? PlayerSessionState.SignedIn)?.profileId
                if (remember && atlasId != null && profileId != null && vault?.available == true) {
                    _appLock.value = AppLockRequest.Save(
                        com.turnhub.android.data.SavedProfile(atlasId, profileId, clean),
                        pin,
                    )
                }
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

    // --- live turn notification ----------------------------------------------

    /**
     * Whose turn it is for the lock-screen notification, kept up to date even
     * while the screen is not watching (the Activity shows it only in the
     * background). Null unless this phone's player sits in a live game.
     */
    val liveTurn: StateFlow<com.turnhub.android.domain.LiveTurn?> = combine(
        repository.tableSummary,
        playerSession.state,
    ) { summary, session ->
        val info = (session as? PlayerSessionState.SignedIn)?.info
        summary?.let {
            com.turnhub.android.domain.LiveTurn.from(
                it,
                info?.takeIf { me -> me.participating }?.playerNumber,
                com.turnhub.android.domain.TableClock.nowMs(),
                System.currentTimeMillis(),
            )
        }
    }.stateIn(viewModelScope, SharingStarted.Eagerly, null)

    // --- app lock and automatic sign-in ---------------------------------------

    private val _appLock = MutableStateFlow<AppLockRequest?>(null)

    /** A fingerprint, face or screen-lock check the Activity should run now. */
    val appLock: StateFlow<AppLockRequest?> = _appLock

    /** Atlas boots this phone already tried (or the player declined) automatic sign-in for. */
    private val autoSignInTried = mutableSetOf<String>()

    private fun offerAutomaticSignIn(atlasId: String) {
        val summary = repository.tableSummary.value ?: return
        val key = "$atlasId/${summary.bootId}"
        if (!autoSignInTried.add(key)) return
        if (playerSession.state.value is PlayerSessionState.SignedIn) return
        val saved = vault?.saved(atlasId) ?: return
        _appLock.value = AppLockRequest.Unlock(saved)
    }

    /** The Activity's check finished; [passed] false when the player cancelled or it failed. */
    fun onAppLockResult(passed: Boolean) {
        val request = _appLock.value ?: return
        _appLock.value = null
        val vault = vault ?: return
        if (!passed) return
        when (request) {
            is AppLockRequest.Save -> {
                vault.save(request.profile, request.secret)
                local.update { it.copy(vaultVersion = it.vaultVersion + 1) }
            }
            is AppLockRequest.Unlock -> {
                val endpoint = repository.endpoint.value ?: return
                val secret = vault.unlock(request.profile.atlasId) ?: run {
                    local.update { it.copy(vaultVersion = it.vaultVersion + 1) }
                    return
                }
                viewModelScope.launch {
                    val profile = ProfileSummary(request.profile.profileId, request.profile.name, hasPin = true)
                    try {
                        playerSession.signIn(endpoint, profile, secret)
                    } catch (e: AtlasException) {
                        // A changed PIN or password: sign in by hand (which saves the new one).
                        local.update {
                            it.copy(
                                signIn = SignInPrompt(
                                    loading = true,
                                    offerRemember = true,
                                    preselect = profile.profileId,
                                ),
                            )
                        }
                        val profiles = runCatching { playerSession.profiles(endpoint) }.getOrDefault(emptyList())
                        local.update { state ->
                            state.copy(
                                signIn = state.signIn?.copy(
                                    loading = false,
                                    profiles = profiles,
                                    error = "Couldn't sign in automatically: ${e.failure.userMessage}",
                                ),
                            )
                        }
                    }
                }
            }
        }
    }

    /** Me → "Sign in automatically" off: forgets this table's saved profile on this phone. */
    fun onForgetSavedProfile() {
        val atlasId = repository.tableSummary.value?.atlasId ?: return
        vault?.forget(atlasId)
        local.update { it.copy(vaultVersion = it.vaultVersion + 1) }
    }

    fun onJoinClicked() {
        launchTable { playerSession.join() }
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
    fun onAccessibilitySaved(sigilSound: Boolean, ledStyle: LedStyle, longPressMs: Int, winHoldMs: Int, lifeApprovalMs: Int) {
        viewModelScope.launch {
            playerSession.saveAccessibility(sigilSound, ledStyle, longPressMs, winHoldMs, lifeApprovalMs)
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
        launchTable {
            playerSession.counter("/api/control/life", listOf("delta" to delta.toString()), "Life updated.")
        }
    }

    /** Asks [target] to approve a change to their life (Atlas accepts it after 15 s unless rejected). */
    fun onRequestLife(target: Int, delta: Int) {
        if (delta == 0) return
        launchTable {
            playerSession.counter(
                "/api/control/life/request",
                listOf("target" to target.toString(), "delta" to delta.toString()),
                "Life change requested.",
            )
        }
    }

    fun onRespondLife(requestId: Long, accept: Boolean) {
        launchTable {
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
        launchTable {
            playerSession.counter(
                "/api/control/commander",
                listOf("source" to source.toString(), "commander" to commander.toString(), "delta" to delta.toString()),
                "Commander damage and life updated.",
            )
        }
    }

    fun onPlayerCounter(kind: String, value: Int, commander: Int) {
        val key = if (kind == "initiative" || kind == "city-blessing") "value" else "delta"
        launchTable { playerSession.counter("/api/control/counter", listOf("counter" to kind, key to "$value", "commander" to "$commander"), "Counter updated.") }
    }

    fun onEliminate(source: Int?) {
        launchTable { playerSession.counter("/api/control/eliminate", listOf("eliminatedBy" to "${source ?: 0}",
            "selfEliminated" to if (source == 0) "1" else "0"), "Elimination recorded.") }
    }

    fun onResolveElimination(victim: Int, source: Int?, token: com.turnhub.android.protocol.GameActionToken) {
        viewModelScope.launch(com.turnhub.android.data.ExpectedGame(token.game)) {
            playerSession.counter("/api/control/elimination", listOf("player" to "$victim", "source" to "${source ?: 0}",
                "selfEliminated" to if (source == 0) "1" else "0", "expectedBootId" to token.bootId,
                "expectedRevision" to "${token.revision}"), "Elimination source recorded.")
        }
    }

    fun onDoom(action: com.turnhub.android.protocol.DoomAction, token: com.turnhub.android.protocol.GameActionToken, player: Int, value: Int) {
        viewModelScope.launch(com.turnhub.android.data.ExpectedGame(token.game)) {
            playerSession.counter("/api/control/doom", listOf("action" to action.wire, "cycle" to "${token.cycle}",
                "expectedRevision" to "${token.revision}", "expectedBootId" to token.bootId,
                "player" to "$player", "inBag" to "$value", "source" to "$value"), "Bag updated.")
        }
    }

    fun onEliminationRule(field: String, value: Int) {
        launchTable { playerSession.counter("/api/game/settings", listOf(field to "$value"), "Elimination settings updated.") }
    }

    fun onSetZeroLifeElimination(enabled: Boolean) {
        launchTable { playerSession.saveGameSettings(null, null, null, autoEliminateAtZeroLife = enabled) }
    }

    fun onSaveGameSettings(gameProfile: String?, startingLife: Int?, turnTimerMs: Long?, twoHeadedGiant: Boolean? = null, houseRules: Boolean? = null) {
        launchTable { playerSession.saveGameSettings(gameProfile, startingLife, turnTimerMs, twoHeadedGiant, houseRules = houseRules) }
    }

    fun onSaveName(name: String) {
        val clean = name.trim()
        if (clean.isEmpty()) return
        viewModelScope.launch { playerSession.saveProfile(clean, null); repository.invalidatePresentationMetadata() }
    }

    fun onSavePin(pin: String) {
        if (!ProfileSecret.isValid(pin)) {
            playerSession.clearFeedback()
            return
        }
        viewModelScope.launch {
            playerSession.saveProfile(null, pin)
            playerSession.loadChoices()
        }
    }

    fun onLoadChoices() {
        viewModelScope.launch { playerSession.loadChoices() }
    }

    fun onSavePolicy(allowPhysicalWithoutPin: Boolean, hideStatsWithoutAuthentication: Boolean) {
        viewModelScope.launch { playerSession.savePolicy(allowPhysicalWithoutPin, hideStatsWithoutAuthentication) }
    }

    fun onLoadStats() {
        viewModelScope.launch { playerSession.loadStats() }
    }

    /** Fetches the shareable statistics report and hands it to [onText]; a failure shows with the statistics. */
    fun onExportStats(onText: (String) -> Unit) {
        viewModelScope.launch { playerSession.exportStats()?.let(onText) }
    }

    fun onClearPin() {
        viewModelScope.launch { playerSession.clearPin() }
    }

    fun onLoadPersonalization() {
        viewModelScope.launch { playerSession.loadPersonalization() }
    }

    /** [color] is `#rrggbb` or `none`; [avatar] 0 clears it. */
    fun onUploadAvatar(bytes: ByteArray, thumbnail: String) { viewModelScope.launch { playerSession.uploadAvatar(bytes, thumbnail); repository.invalidatePresentationMetadata() } }
    fun onRemoveAvatar() { viewModelScope.launch { playerSession.removeAvatar(); repository.invalidatePresentationMetadata() } }

    fun onSavePersonalization(color: String?, avatar: Int?) {
        viewModelScope.launch { playerSession.savePersonalization(color, avatar); repository.invalidatePresentationMetadata() }
    }

    /** Runs one admin/developer request; the outcome shows in [adminState]. */
    fun onAdmin(block: suspend com.turnhub.android.data.AtlasAdminConsole.() -> Unit) {
        viewModelScope.launch { adminConsole.block(); repository.invalidatePresentationMetadata() }
    }

    fun onAdminMessageDismissed() = adminConsole.clearMessage()

    // --- tablet mode --------------------------------------------------------------

    /** Runs one tablet request; the outcome shows in [tabletState] and the next state snapshot. */
    fun onTablet(block: suspend com.turnhub.android.data.AtlasTablet.() -> Unit) {
        viewModelScope.launch { tablet.block() }
    }

    /**
     * Play together's view of the connected Atlas game: guests get Atlas's state through
     * this device, and their taps go to Atlas through its tablet grant, for their own seat.
     */
    internal val lanAtlasLink = object : com.turnhub.android.lan.LanAtlasLink {
        override fun summary() = repository.tableSummary.value
        override fun canAct() = (playerSession.state.value as? PlayerSessionState.SignedIn)?.info?.tablet == true
        override suspend fun act(game: Int, block: suspend com.turnhub.android.data.TableControls.() -> Boolean): Boolean {
            var applied = false
            tablet.inGame(game) { applied = block() }
            return applied
        }
    }

    fun onTabletMessageDismissed() = tablet.clearMessage()

    fun onTabletPinDismissed() = tablet.dismissPin()

    fun onTabletCodeDismissed() = tablet.dismissCode()

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
    private var discoveryJob: Job? = null
    private var wifiJoinJob: Job? = null
    private var connectJob: Job? = null
    private var disconnectJob: Job? = null
    private var automaticProbe = false

    /** Once-per-launch probe on the current network, without Wi-Fi or permission dialogs. */
    fun onAppStarted() {
        if (autoStarted) return
        autoStarted = true
        automaticProbe = true
        // Quiet probe on the phone's current network. Never ask Android to join
        // Wi-Fi, request permission or replace local play during automatic discovery.
        discoveryJob = viewModelScope.launch {
            disconnectJob?.join()
            if (idle() && credentialStore.lastSsid() != null) repository.connect(AtlasEndpoint.DEFAULT)
        }
    }

    fun onPresentationForeground(visible: Boolean) = repository.setPresentationForeground(visible)

    fun onAtlasDiscoveryEnabled() { autoStarted = false; onAppStarted() }

    fun onAtlasOpened() {
        automaticProbe = false
        repository.tableSummary.value?.let { offerAutomaticSignIn(it.atlasId) }
    }

    /** "Try again" after the saved table didn't answer. */
    fun onSearchAgain() {
        automaticProbe = false
        if (discoveryJob?.isActive == true) return
        discoveryJob = viewModelScope.launch {
            disconnectJob?.join()
            rejoinSavedTable()
        }
    }

    /**
     * "Set up a new table": a new or factory-reset Atlas uses the printed
     * password, and Android's own dialog lists it; setup opens once it answers.
     */
    fun onSetUpNewTable() {
        automaticProbe = false
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
            screen.joiningSsid == null && screen.wifiPrompt == null && !screen.wifiOff && !screen.rejoining
    }

    private suspend fun rejoinSavedTable() {
        if (!idle()) return
        val saved = credentialStore.lastSsid()?.let(credentialStore::load)
        if (saved == null) {
            // A phone that has never joined a table: the screen offers setup.
            local.update { it.copy(discovery = Discovery.Idle) }
            return
        }
        if (!wifiLink.wifiEnabled()) return askToTurnOnWifi { onSearchAgain() }
        local.update { it.copy(discovery = Discovery.Searching, failure = null) }
        val joined = quietJoin(AtlasEndpoint.DEFAULT, saved)
        local.update { it.copy(discovery = if (joined) Discovery.Idle else Discovery.NotFound) }
    }

    /** Joins and connects; false (and no prompt) if Android couldn't join. */
    private suspend fun quietJoin(endpoint: AtlasEndpoint, credentials: WifiCredentials): Boolean {
        local.update { it.copy(joiningSsid = credentials.ssid, failure = null) }
        val result = wifiLink.join(credentials)
        currentCoroutineContext().ensureActive()
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

    private fun launchTable(block: suspend () -> Unit) {
        val game = repository.tableSummary.value?.game ?: return
        viewModelScope.launch(com.turnhub.android.data.ExpectedGame(game)) { block() }
    }

    /** Sends once, pinned to the snapshot the player was looking at. */
    private fun sendControl(action: ControlAction) {
        val summary = repository.tableSummary.value ?: return
        viewModelScope.launch(com.turnhub.android.data.ExpectedGame(summary.game)) { playerSession.control(action, summary.revision, summary.bootId) }
    }

    fun onEndpointChanged(text: String) {
        local.update { it.copy(endpointText = text, failure = null) }
    }

    /**
     * Starts connecting to the entered endpoint. The Activity calls this only
     * once the platform's local-network permission is granted (or not required).
     */
    fun onConnectClicked() {
        automaticProbe = false
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
        if (!wifiLink.wifiEnabled()) return askToTurnOnWifi { onConnectClicked() }
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

    /** Wi-Fi is off: ask first, and retry [retry] once it is on. */
    private fun askToTurnOnWifi(retry: () -> Unit) {
        afterWifiOn = retry
        local.update { it.copy(wifiOff = true, discovery = Discovery.Idle) }
    }

    /**
     * The user chose to turn Wi-Fi on (the Activity opens Android's Wi-Fi
     * panel). The connection carries on by itself once Wi-Fi is on.
     */
    fun onTurnOnWifi() {
        val retry = afterWifiOn ?: return
        afterWifiOn = null
        local.update { it.copy(wifiOff = false, discovery = Discovery.Searching) }
        wifiJoinJob?.cancel()
        wifiJoinJob = viewModelScope.launch {
            val on = wifiLink.awaitWifiEnabled(WIFI_ON_TIMEOUT_MS)
            local.update { it.copy(discovery = Discovery.Idle) }
            if (on) retry() else local.update { it.copy(failure = AtlasFailure.WifiOff) }
        }
    }

    fun onWifiOffDismissed() {
        afterWifiOn = null
        local.update { it.copy(wifiOff = false) }
    }

    fun onWifiPromptDismissed() {
        pendingEndpoint = null
        local.update { it.copy(wifiPrompt = null) }
    }

    fun onDisconnectClicked() {
        // A device-play choice must also stop a join that has not answered yet.
        // Otherwise its late result can reconnect and poll behind the local game.
        localImportJob?.cancel()
        _localImport.value = com.turnhub.android.standalone.LocalImportState()
        discoveryJob?.cancel()
        wifiJoinJob?.cancel()
        connectJob?.cancel()
        pendingEndpoint = null
        local.update {
            it.copy(discovery = Discovery.Idle, joiningSsid = null, wifiPrompt = null, failure = null)
        }
        disconnectJob = viewModelScope.launch {
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
        if (!wifiLink.wifiEnabled()) return askToTurnOnWifi { joinThenConnect(endpoint, credentials) }
        local.update { it.copy(joiningSsid = credentials.ssid, failure = null) }
        wifiJoinJob?.cancel()
        wifiJoinJob = viewModelScope.launch {
            disconnectJob?.join()
            val result = wifiLink.join(credentials)
            currentCoroutineContext().ensureActive()
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
        connectJob?.cancel()
        connectJob = viewModelScope.launch {
            disconnectJob?.join()
            repository.connect(endpoint)
        }
    }

    companion object {

        /** Atlas answers, then restarts about a second later. */
        private const val RESTART_GRACE_MS = 4_000L
        /** How long to wait for Wi-Fi after the user chose to turn it on. */
        private const val WIFI_ON_TIMEOUT_MS = 60_000L
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
            vault: com.turnhub.android.data.ProfileVault? = null,
        ): ViewModelProvider.Factory = viewModelFactory {
            initializer {
                val releases = GitHubFirmwareReleases()
                HomeViewModel(
                    repositoryFactory, wifiLink, credentialStore, playerSession, releases,
                    AtlasUpdateWatcher(playerSession, releases, updateCheckIntervalMs),
                    vault,
                )
            }
        }
    }
}

