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
    private val offlineQueues: com.turnhub.android.data.OfflineQueueStore = com.turnhub.android.data.MemoryOfflineQueueStore(),
    /** Atlas on the home network over pinned HTTPS (HOME_WIFI.md); null in tests. */
    private val homeLink: com.turnhub.android.data.AtlasHomeLink? = null,
    /** Show this phone's game on Atlas as soon as it is connected and signed in; off in tests. */
    private val autoHost: Boolean = false,
) : ViewModel() {

    private val repository: AtlasRepository = repositoryFactory(viewModelScope)

    /**
     * Local game/library owner. Atlas delivery requires an explicit per-match
     * mapping; connected profiles never become local identity by name.
     */
    /** Phone-hosted games (protocol/hosted-v1.md): this phone's game shown on Atlas. */
    private val hostedLink = com.turnhub.android.data.HostedAtlasLink({ method, path, fields -> playerSession.raw(method, path, fields) })
    val hostedStatus: StateFlow<com.turnhub.android.data.HostedAtlasLink.Status> = hostedLink.status
    private var hostedJob: Job? = null

    /** Start or stop showing this phone's game on the connected Atlas. The phone keeps running it. */
    fun onHostOnAtlas(on: Boolean) {
        // The player's own Stop holds until this phone leaves the Atlas.
        hostingStopped = !on
        hostedJob?.cancel()
        hostedJob = null
        _hostedResume.value = null
        val table = standalone ?: return
        if (!on) return
        hostedJob = viewModelScope.launch {
            // Another phone's unfinished game on Atlas: offer it first, unless this phone is mid-game.
            val underway = table.state.value.game.state.let {
                it == com.turnhub.android.protocol.TableState.STARTING || it == com.turnhub.android.protocol.TableState.RUNNING ||
                    it == com.turnhub.android.protocol.TableState.PAUSED
            }
            if (!underway) hostedLink.resumable()?.let { _hostedResume.value = it; return@launch }
            host(table)
        }
    }

    /** The unfinished game Atlas kept from its last hub, offered before this phone shows its own. */
    private val _hostedResume = MutableStateFlow<com.turnhub.android.data.HostedAtlasLink.Resume?>(null)
    val hostedResume: StateFlow<com.turnhub.android.data.HostedAtlasLink.Resume?> = _hostedResume.asStateFlow()

    /** Resume Atlas's kept game here (or start this phone's own instead), then show it on Atlas. */
    fun onResumeHosted(resume: Boolean) {
        val offer = _hostedResume.value ?: return
        val table = standalone ?: return
        _hostedResume.value = null
        if (resume) table.resumeHostedGame(offer.game)
        hostedJob?.cancel()
        hostedJob = viewModelScope.launch { host(table) }
    }

    private suspend fun host(table: com.turnhub.android.standalone.StandaloneTable) {
        hostedLink.host(table.state.map { hostedGameJson(it.game) }) { input ->
            // Atlas's Sigils and screen: this phone's rules decide.
            table.applyHostedInput(input.player, input.action)
        }
    }

    /** The player stopped showing this phone's game on the connected Atlas. */
    private var hostingStopped = false
    private var autoHostObserver: Job? = null

    /**
     * Phone-hosted games are the one way to play with Atlas (owner, 2026-11-04):
     * once this phone is connected and signed in, it shows its game on Atlas
     * without a tap, unless the player stopped it. Leaving the Atlas resets that.
     */
    private fun observeAutoHost() {
        autoHostObserver?.cancel()
        autoHostObserver = viewModelScope.launch {
            // A game Atlas still runs itself keeps the table until it ends.
            combine(repository.connectionState, repository.offlineSinceMs, playerSession.state,
                repository.tableSummary.map { it?.state?.underway() == true }) { state, since, session, atlasGame ->
                state == AtlasConnectionState.CONNECTED && since == null && session is PlayerSessionState.SignedIn && !atlasGame
            }.distinctUntilChanged().collect { ready ->
                if (!ready) {
                    if (repository.connectionState.value == AtlasConnectionState.DISCONNECTED) hostingStopped = false
                    return@collect
                }
                if (!hostingStopped && hostedJob?.isActive != true && _hostedResume.value == null &&
                    hostedLink.status.value !is com.turnhub.android.data.HostedAtlasLink.Status.Busy) onHostOnAtlas(true)
            }
        }
    }

    private var standaloneObserver: Job? = null
    var standalone: com.turnhub.android.standalone.StandaloneTable? = null
        set(value) {
            field = value
            standaloneObserver?.cancel()
            if (value != null && autoHost) observeAutoHost()
            if (value != null) standaloneObserver = viewModelScope.launch {
                combine(value.state.map { it.library.history.map { r -> r.recordId } to it.library.players.map { p -> p.links } }.distinctUntilChanged(),
                    repository.tableSummary.map { it?.atlasId to it?.bootId }.distinctUntilChanged(),
                    playerSession.state.map { (it as? PlayerSessionState.SignedIn)?.profileId }.distinctUntilChanged(),
                    combine(repository.connectionState, repository.offlineSinceMs) { state, since -> state == AtlasConnectionState.CONNECTED && since == null }.distinctUntilChanged()) { _, epoch, signedIn, online -> Triple(epoch, signedIn, online) }
                    .collect { (epoch, _, online) -> if (online && epoch.first != null) onLoadLocalImportProfiles() }
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
                    if ((playerSession.state.value as? PlayerSessionState.SignedIn)?.profileId?.isNotBlank() == true) sendAutomaticLocalRecords(summary.atlasId, profiles)
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
     * Gets an Atlas account onto this phone (a fresh install or a new device): checks its
     * PIN on Atlas, recreates it here under the same ID and links it; profile sync and the
     * Atlas totals take over from there.
     */
    fun onRestoreAccount(profileId: String, secret: String) {
        val summary = repository.tableSummary.value ?: return
        val endpoint = repository.endpoint.value ?: return
        val table = standalone ?: return
        if (localImportJob?.isActive == true || repository.connectionState.value != AtlasConnectionState.CONNECTED) return
        val fallbackName = _localImport.value.profiles.firstOrNull { it.profileId == profileId }?.name.orEmpty()
        _localImport.value = _localImport.value.copy(busy = true, message = null)
        localImportJob = viewModelScope.launch {
            try {
                val restored = playerSession.restoreAccount(endpoint, profileId, secret)
                val credential = kotlinx.coroutines.withContext(kotlinx.coroutines.Dispatchers.Default) {
                    com.turnhub.android.standalone.LocalCredential.create(secret) }
                if (repository.tableSummary.value?.atlasId != summary.atlasId || repository.endpoint.value != endpoint) return@launch
                val look = com.turnhub.android.standalone.LocalPersonalization(color = restored.color, avatar = restored.avatar,
                    reduceMotion = restored.reduceMotion)
                val name = restored.name.ifBlank { fallbackName }
                if (!table.restoreAccount(summary.atlasId, restored.profileId, name, look, restored.linkKey, credential)) {
                    _localImport.value = _localImport.value.copy(message = "Could not add this account here, or it's already on this device.")
                    return@launch
                }
                val games = restored.linkKey?.let { pullAtlasHistory(endpoint, summary.atlasId, restored.profileId, it) } ?: 0
                _localImport.value = _localImport.value.copy(message = when (games) {
                    0 -> "$name is on this device again, with their totals from this Atlas."
                    1 -> "$name is on this device again, with 1 game from this Atlas."
                    else -> "$name is on this device again, with $games games from this Atlas."
                })
                syncLinkedProfiles(summary, endpoint)
            } catch (e: kotlinx.coroutines.CancellationException) { throw e }
            catch (e: Exception) {
                _localImport.value = _localImport.value.copy(message = (e as? AtlasException)?.failure?.userMessage
                    ?: (e as? IllegalArgumentException)?.message ?: "Could not get the account from Atlas.")
            } finally { _localImport.value = _localImport.value.copy(busy = false) }
        }
    }

    /** Pages through an account's match records on Atlas into this device's history; returns how many were added. */
    private suspend fun pullAtlasHistory(endpoint: AtlasEndpoint, atlasId: String, profileId: String, linkKey: String): Int {
        val table = standalone ?: return 0
        val records = mutableListOf<org.json.JSONObject>()
        var skip = 0
        // At most 100 pages (2,000 games); older Atlas firmware has no history (404).
        repeat(100) {
            val response = try { playerSession.profileHistory(endpoint, profileId, linkKey, skip) } catch (_: AtlasException) { return table.addAtlasRecords(atlasId, records) }
            val page = runCatching { org.json.JSONObject(response.body) }.getOrNull()
            if (!response.ok || page == null) return table.addAtlasRecords(atlasId, records)
            val items = page.optJSONArray("records") ?: return table.addAtlasRecords(atlasId, records)
            for (i in 0 until items.length()) items.optJSONObject(i)?.let(records::add)
            skip += items.length()
            if (!page.optBoolean("more") || items.length() == 0) return table.addAtlasRecords(atlasId, records)
        }
        return table.addAtlasRecords(atlasId, records)
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
                if ((playerSession.state.value as? PlayerSessionState.SignedIn)?.profileId?.isNotBlank() == true) sendAutomaticLocalRecords(summary.atlasId, profiles)
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
                if ((playerSession.state.value as? PlayerSessionState.SignedIn)?.profileId?.isNotBlank() == true) sendAutomaticLocalRecords(summary.atlasId, profiles)
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
                add("createdAtMs" to "${player.createdAtMs}")
                add("name" to player.name); add("color" to (look.color ?: "none"))
                // An uploaded image stays on the phone; the avatar only syncs a preset.
                if (look.image == null) add("avatar" to "${presets.firstOrNull { it.key == look.avatar }?.id ?: 0}")
                add("reduceMotion" to if (look.reduceMotion) "1" else "0")
            }
            val response = try { playerSession.syncProfile(endpoint, fields) } catch (_: AtlasException) { continue }
            if (response.code == 401) { table.dropLinkKey(player.localId, summary.atlasId); continue }
            val atlas = runCatching { org.json.JSONObject(response.body) }.getOrNull() ?: continue
            if (response.code == 409) {
                val target = atlas.optString("mergedInto")
                val key = atlas.optString("linkKey")
                if (com.turnhub.android.standalone.LocalProfileIds.valid(target) && key.matches(Regex("[0-9A-F]{32}")) &&
                    !table.linkAccount(player.localId, summary.atlasId, target, key))
                    _localImport.value = _localImport.value.copy(message = "The merged Atlas account is already linked to another local profile. Local histories were kept; review the account links.")
                continue
            }
            if (!response.ok) continue
            table.keepProfileCreationTime(player.localId, atlas.optLong("createdAtMs"))
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

    /** Atlas's whole game while one runs: the start of [offlineGame] if Atlas drops off. */
    @Volatile private var offlineBase: com.turnhub.android.data.OfflineBase? = null
    private val restoredOffline = runCatching { offlineQueues.load()?.let { com.turnhub.android.data.OfflineAtlasGame.restore(it, offlineQueues::save) } }
    private val _offlineGame = MutableStateFlow(restoredOffline.getOrNull())
    /** The game going on on this device while Atlas is out of reach; null otherwise. */
    val offlineGame: StateFlow<com.turnhub.android.data.OfflineAtlasGame?> = _offlineGame.asStateFlow()

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
        val homePairing: com.turnhub.android.data.SavedHomeAtlas? = null,
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
            hasSavedTable = credentialStore.lastSsid() != null || homeLink?.hasSavedAtlas() == true,
            homePairing = screen.homePairing,
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
        // Offline play (protocol/http-v1.md, "Offline play"): while a game runs,
        // keep Atlas's whole game, so the table goes on here if Atlas drops off.
        viewModelScope.launch {
            repository.tableSummary
                .map { it?.takeIf { s -> s.state == com.turnhub.android.protocol.TableState.RUNNING || s.state == com.turnhub.android.protocol.TableState.PAUSED } }
                .distinctUntilChanged { a, b -> a?.atlasId == b?.atlasId && a?.bootId == b?.bootId && a?.game == b?.game && a?.revision == b?.revision }
                .collectLatest { summary ->
                    if (summary == null) { offlineBase = null; return@collectLatest }
                    if (repository.offlineSinceMs.value != null) return@collectLatest
                    tablet.fetchOfflineBase(summary.game, System.currentTimeMillis())
                        ?.takeIf { it.atlasId == summary.atlasId && it.bootId == summary.bootId && it.gameId == summary.matchId }
                        ?.let { offlineBase = it }
                }
        }
        // Keep an unacknowledged queue across reconnects and process recreation.
        // Never route it into another Atlas, venue table or rematch.
        viewModelScope.launch {
            var retryAt = 0L
            if (restoredOffline.isFailure) tablet.offlineNotice("Saved offline moves could not be read. They were kept untouched; recovery needs review.")
            combine(repository.offlineSinceMs, repository.tableSummary, playerSession.state) { since, summary, session ->
                listOf(since != null, summary?.atlasId, summary?.bootId, summary?.game, summary?.matchId,
                    session is PlayerSessionState.SignedIn).joinToString("|")
            }.distinctUntilChanged().collectLatest {
                while (repository.offlineSinceMs.value != null || _offlineGame.value != null && repository.tableSummary.value != null) {
                    val since = repository.offlineSinceMs.value
                    val summary = repository.tableSummary.value
                    var played = _offlineGame.value
                    if (since != null && played == null && !restoredOffline.isFailure) {
                        offlineBase?.takeIf { base -> base.playing && summary != null && base.matches(summary) && base.bootId == summary.bootId }?.let { base ->
                            val candidate = com.turnhub.android.data.OfflineAtlasGame(base, persist = offlineQueues::save)
                            if (candidate.save()) { candidate.listen(); _offlineGame.value = candidate; played = candidate }
                            else tablet.offlineNotice("Couldn't save offline play. Reconnect Atlas before making more edits.")
                        }
                    }
                    val pending = played
                    if (pending != null) {
                        if (since != null && summary != null && pending.base.matches(summary) && pending.base.bootId == summary.bootId) pending.listen()
                        else {
                            pending.stop()
                            if (since == null && summary != null && pending.base.matches(summary) &&
                                repository.connectionState.value == AtlasConnectionState.CONNECTED && System.currentTimeMillis() >= retryAt) {
                                val delivered = tablet.syncOffline(pending)
                                if (delivered == true && pending.acknowledge()) {
                                    _offlineGame.value = null
                                    offlineBase = tablet.fetchOfflineBase(summary.game, System.currentTimeMillis())
                                    tablet.replayOffline(summary, com.turnhub.android.domain.TableClock.nowMs())
                                } else {
                                    retryAt = System.currentTimeMillis() + if (delivered == false) 30_000 else 5_000
                                    if (delivered == null) tablet.offlineNotice("Offline moves are saved on this device. Waiting for Atlas to confirm them.")
                                    if (delivered == true) tablet.offlineNotice("Atlas confirmed the moves, but this device couldn't clear its saved queue. Free storage and retry.")
                                }
                            }
                        }
                    }
                    delay(2_000)
                }
            }
        }
        viewModelScope.launch {
            repository.tableSummary.collect { summary -> summary?.let { tablet.settleOffline(it.revision) } }
        }
        // The bounded life/damage fallback still needs replay when no full-core base was available.
        viewModelScope.launch {
            repository.offlineSinceMs.collect { since ->
                val summary = repository.tableSummary.value
                if (since == null && summary != null && _offlineGame.value?.base?.matches(summary) != true)
                    tablet.replayOffline(summary, com.turnhub.android.domain.TableClock.nowMs())
            }
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
        if (pin.isNotEmpty() && !ProfileSecret.isValid(pin)) {
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
                if (remember && pin.isNotEmpty() && atlasId != null && vault?.available == true) {
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
            pin.isNotEmpty() && !ProfileSecret.isValid(pin) -> ProfileSecret.RULE
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
                if (remember && pin.isNotEmpty() && atlasId != null && profileId != null && vault?.available == true) {
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

    fun onSavePin(pin: String, currentPin: String?) {
        if (!ProfileSecret.isValid(pin)) {
            playerSession.clearFeedback()
            return
        }
        viewModelScope.launch {
            playerSession.saveProfile(null, pin, currentPin)
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

    fun onClearPin(currentPin: String) {
        viewModelScope.launch { playerSession.clearPin(currentPin) }
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
        if (waitingForOfflineSync()) { tablet.offlineNotice("Offline moves are still waiting for Atlas to confirm."); return }
        viewModelScope.launch { tablet.block() }
    }

    /**
     * Play together's view of the connected Atlas game: guests get Atlas's state through
     * this device, and their taps go to Atlas through its tablet grant, for their own seat.
     */
    internal val lanAtlasLink = object : com.turnhub.android.lan.LanAtlasLink {
        override fun summary() = repository.tableSummary.value
        override fun canAct() = playerSession.state.value is PlayerSessionState.SignedIn && repository.offlineSinceMs.value == null &&
            _offlineGame.value?.base?.let { base -> summary()?.let(base::matches) } != true
        private var directoryAt = 0L
        private var directoryAtlas = ""
        private var directory = org.json.JSONArray()
        override suspend fun roomProfiles(): org.json.JSONArray {
            val endpoint = repository.endpoint.value ?: return org.json.JSONArray()
            val now = android.os.SystemClock.elapsedRealtime()
            val atlas = summary()?.atlasId.orEmpty()
            if (directoryAtlas != atlas) { directory = org.json.JSONArray(); directoryAt = 0; directoryAtlas = atlas }
            if (now - directoryAt > 30_000 || directoryAt == 0L) {
                val profiles = try { playerSession.profiles(endpoint) } catch (_: com.turnhub.android.data.AtlasException) { return directory }
                directory = org.json.JSONArray(profiles.map {
                    org.json.JSONObject().put("profileId", it.profileId).put("name", it.name).put("hasPin", it.hasPin).put("aliases", org.json.JSONArray(it.aliases))
                })
                directoryAt = now
            }
            return directory
        }
        override fun resolveIdentity(id: String): String {
            for (i in 0 until directory.length()) {
                val profile = directory.getJSONObject(i)
                val aliases = profile.optJSONArray("aliases") ?: continue
                if ((0 until aliases.length()).any { aliases.getString(it) == id }) return profile.getString("profileId")
            }
            return id
        }
        override suspend fun attachProfile(request: org.json.JSONObject): String? {
            val endpoint = repository.endpoint.value ?: return null
            request.put("_atlasId", summary()?.atlasId ?: return null)
            roomProfiles()
            val original = request.optString("profileId").ifBlank { request.optJSONObject("profile")?.optString("profileId").orEmpty() }
            request.put("profileId", resolveIdentity(original))
            val id = try { playerSession.attachRoomProfile(endpoint, request) } catch (_: com.turnhub.android.data.AtlasException) { null } ?: return null
            directoryAt = 0
            val summary = summary()
            if (summary?.state == com.turnhub.android.protocol.TableState.LOBBY && summary.players.none { it.profileId == id }) {
                tablet.inGame(summary.game) { seatSaved(id, request.optString("name")) }
            }
            return id
        }
        override suspend fun backupRoom(record: org.json.JSONObject): Boolean =
            playerSession.raw("POST", "/api/room", listOf("roomId" to record.getString("session"),
                "ownerKey" to record.getString("ownerKey"), "record" to record.toString()))?.ok == true
        override suspend fun restoreRoom(record: org.json.JSONObject): org.json.JSONObject? {
            val response = playerSession.raw("POST", "/api/room", listOf("roomId" to record.getString("session"),
                "ownerKey" to record.getString("ownerKey"))) ?: return null
            return if (response.ok) org.json.JSONObject(response.body) else null
        }
        override suspend fun endRoom(record: org.json.JSONObject): Boolean =
            playerSession.raw("POST", "/api/room", listOf("roomId" to record.getString("session"),
                "ownerKey" to record.getString("ownerKey"), "end" to "1"))?.ok == true
        override suspend fun act(game: Int, block: suspend com.turnhub.android.data.TableControls.() -> Boolean): Boolean {
            if (!canAct()) return false
            var applied = false
            tablet.clearMessage()
            tablet.inGame(game, summary()?.matchId) { applied = block() }
            return applied && tablet.state.value.message?.isError != true
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
            if (!idle()) return@launch
            // Home Wi-Fi first: Atlas's own network is the fallback.
            val home = homeLink?.find()
            if (home != null && idle()) {
                repository.connect(home)
            } else if (idle() && credentialStore.lastSsid() != null) {
                repository.connect(AtlasEndpoint.DEFAULT)
                learnHome(AtlasEndpoint.DEFAULT)
            }
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
        val factoryDefault = WifiCredentials(WifiCredentials.DEFAULT_ATLAS_SSID, WifiCredentials.DEFAULT_ATLAS_PASSPHRASE)
        val link = homeLink
        // A table this phone already trusts is never "new": skip home search and use the printed password.
        if (link == null || link.hasSavedAtlas() || !wifiLink.wifiEnabled()) return joinThenConnect(AtlasEndpoint.DEFAULT, factoryDefault)
        wifiJoinJob?.cancel()
        wifiJoinJob = viewModelScope.launch {
            disconnectJob?.join()
            local.update { it.copy(discovery = Discovery.Searching) }
            // A fresh app may find an Atlas already on home Wi-Fi; trusting it still needs the App link code.
            val candidate = link.pairingCandidate()
            currentCoroutineContext().ensureActive()
            local.update { it.copy(discovery = Discovery.Idle, homePairing = candidate) }
            if (candidate == null) joinThenConnect(AtlasEndpoint.DEFAULT, factoryDefault)
        }
    }

    /** Nothing else is joining, connected, rejoining or asking. */
    private fun idle(): Boolean {
        val screen = local.value
        return repository.connectionState.value == AtlasConnectionState.DISCONNECTED &&
            screen.joiningSsid == null && screen.wifiPrompt == null && screen.homePairing == null && !screen.wifiOff && !screen.rejoining
    }

    private suspend fun rejoinSavedTable() {
        if (!idle()) return
        val saved = credentialStore.lastSsid()?.let(credentialStore::load)
        val link = homeLink
        if (saved == null && link?.hasSavedAtlas() != true) {
            // A phone that has never joined a table: offer an Atlas found on home Wi-Fi, else setup.
            if (link == null || !wifiLink.wifiEnabled()) { local.update { it.copy(discovery = Discovery.Idle) }; return }
            local.update { it.copy(discovery = Discovery.Searching, failure = null) }
            val candidate = link.pairingCandidate()
            currentCoroutineContext().ensureActive()
            local.update { it.copy(discovery = Discovery.Idle, homePairing = candidate) }
            return
        }
        if (!wifiLink.wifiEnabled()) return askToTurnOnWifi { onSearchAgain() }
        local.update { it.copy(discovery = Discovery.Searching, failure = null) }
        link?.find()?.let { home ->
            local.update { it.copy(discovery = Discovery.Idle) }
            connectRepository(home)
            return
        }
        if (saved == null) { local.update { it.copy(discovery = Discovery.NotFound) }; return }
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
    private fun waitingForOfflineSync(): Boolean = repository.offlineSinceMs.value == null &&
        _offlineGame.value?.base?.let { base -> repository.tableSummary.value?.let(base::matches) } == true

    private fun sendControl(action: ControlAction) {
        if (waitingForOfflineSync()) { tablet.offlineNotice("Offline moves are still waiting for Atlas to confirm."); return }
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
        if (local.value.joiningSsid != null || local.value.homePairing != null ||
            (wifiJoinJob?.isActive == true && local.value.discovery == Discovery.Searching)) return
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
        val link = homeLink
        if (link == null) return joinAtlasWifi(endpoint)
        wifiJoinJob?.cancel()
        wifiJoinJob = viewModelScope.launch {
            disconnectJob?.join()
            // Home Wi-Fi first; Atlas's own network only if it doesn't answer there.
            local.update { it.copy(discovery = Discovery.Searching) }
            val home = link.find()
            currentCoroutineContext().ensureActive()
            val candidate = if (home == null) link.pairingCandidate() else null
            currentCoroutineContext().ensureActive()
            local.update { it.copy(discovery = Discovery.Idle) }
            when {
                home != null -> connectRepository(home)
                candidate != null -> local.update { it.copy(homePairing = candidate) }
                else -> joinAtlasWifi(endpoint)
            }
        }
    }

    fun onDismissHomePairing() {
        local.update { it.copy(homePairing = null) }
    }

    fun onConfirmHomePairing() {
        val candidate = local.value.homePairing ?: return
        local.update { it.copy(homePairing = null, discovery = Discovery.Searching) }
        wifiJoinJob = viewModelScope.launch {
            val endpoint = homeLink?.confirmPairing(candidate)
            local.update { it.copy(discovery = Discovery.Idle, failure = if (endpoint == null)
                AtlasFailure.Rejected("Atlas could not be verified. Search again and compare the link code on Atlas.") else null) }
            if (endpoint != null) connectRepository(endpoint)
        }
    }

    private fun joinAtlasWifi(endpoint: AtlasEndpoint) {
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

    /** Over Atlas's own network: remember where it answers on the home network. */
    private suspend fun learnHome(endpoint: AtlasEndpoint) {
        if (repository.connectionState.value == AtlasConnectionState.CONNECTED) homeLink?.learn(endpoint)
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
            it.copy(discovery = Discovery.Idle, joiningSsid = null, wifiPrompt = null, homePairing = null, failure = null)
        }
        hostedJob?.cancel()
        hostedJob = null
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
        _offlineGame.value?.stop()
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
            learnHome(endpoint)
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
            offlineQueues: com.turnhub.android.data.OfflineQueueStore = com.turnhub.android.data.MemoryOfflineQueueStore(),
            homeLink: com.turnhub.android.data.AtlasHomeLink? = null,
        ): ViewModelProvider.Factory = viewModelFactory {
            initializer {
                val releases = GitHubFirmwareReleases()
                HomeViewModel(
                    repositoryFactory, wifiLink, credentialStore, playerSession, releases,
                    AtlasUpdateWatcher(playerSession, releases, updateCheckIntervalMs),
                    vault, offlineQueues, homeLink, autoHost = true,
                )
            }
        }
    }
}

/** The core's game JSON, as the shared rules core reads it (NativeGameCore). */
internal fun hostedGameJson(game: com.turnhub.android.standalone.StandaloneGame): org.json.JSONObject =
    com.turnhub.android.standalone.StandaloneTable.gameToJson(game).put("profile", game.profile.name)
