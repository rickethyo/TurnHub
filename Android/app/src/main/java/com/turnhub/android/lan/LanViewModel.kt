package com.turnhub.android.lan

import android.content.Context
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.turnhub.android.protocol.TableState
import com.turnhub.android.standalone.StandaloneGame
import com.turnhub.android.standalone.StandaloneTable
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.collect
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import org.json.JSONObject
import java.net.InetSocketAddress
import java.util.UUID

/** Hosting is one capability; a future store entitlement does not gate joining. */
internal fun interface LanHostingAccess { fun allowed(): Boolean }

internal data class LanUiState(
    val host: LanInvite? = null,
    val guests: List<LanGuest> = emptyList(),
    val preview: StandaloneGame? = null,
    val remote: StandaloneGame? = null,
    val role: String? = null,
    val player: Int = 0,
    val online: Boolean = false,
    val busy: Boolean = false,
    val uncertain: Boolean = false,
    val message: String? = null,
)

internal class LanViewModel(
    context: Context,
    private val table: StandaloneTable,
    private val hostingAccess: LanHostingAccess = LanHostingAccess { true },
) : ViewModel() {
    private val context = context.applicationContext
    private val _state = MutableStateFlow(LanUiState())
    val state = _state.asStateFlow()
    private var server: LanServer? = null
    private var hostingMatch: String? = null
    private var sessions: LanSessions? = null
    private var foreground = true
    private var invite: LanInvite? = null
    private var token: String? = null
    private var pending: JSONObject? = null
    private var joinRequest: JSONObject? = null
    private var polling: Job? = null
    private var generation = 0
    private val exchange = Mutex()

    init {
        viewModelScope.launch {
            table.state.collect { local ->
                if (server != null && (local.game.gameId != hostingMatch || !local.game.profile.deathMayDie)) stopHosting()
            }
        }
    }

    fun setForeground(value: Boolean) { foreground = value }
    fun permissionDenied() { _state.update { it.copy(message = "Allow Nearby devices in Android settings to host or join over Wi-Fi.") } }

    fun startHosting() {
        if (_state.value.busy || token != null || server != null) return
        if (table.state.value.storageProblem != null) {
            _state.update { it.copy(message = "Resolve the local game storage problem before hosting.") }; return
        }
        if (!hostingAccess.allowed()) { _state.update { it.copy(message = "Hosting is not available for this account.") }; return }
        val game = table.state.value.game
        if (!game.profile.deathMayDie || game.state !in listOf(TableState.RUNNING, TableState.PAUSED)) {
            _state.update { it.copy(message = "Start a Death May Die game on this device before sharing it.") }; return
        }
        _state.update { it.copy(busy = true, message = null) }
        viewModelScope.launch {
            var opened: LanServer? = null
            try {
                val session = randomLanSecret(16)
                val key = randomLanSecret()
                val core = LanSessions(session, key, { table.state.value.game }, table::applyLanAction,
                    unavailable = { !foreground || table.state.value.storageProblem != null })
                sessions = core
                val invitation = withContext(Dispatchers.IO) {
                    val wifi = currentLanWifi(context)
                    val (tls, pin) = lanServerContext()
                    val socket = tls.serverSocketFactory.createServerSocket()
                    try {
                        socket.bind(InetSocketAddress(wifi.address, 0), 8)
                        val listening = LanServer(socket) { request ->
                            runBlocking(Dispatchers.Main.immediate) {
                                val reply = if (sessions === core) core.handle(request) else JSONObject()
                                    .put("v", LAN_VERSION).put("session", session).put("code", "ended")
                                _state.update { it.copy(guests = sessions?.guests.orEmpty()) }
                                reply
                            }
                        }
                        opened = listening
                        LanInvite(wifi.address.hostAddress!!, listening.port, pin, session, key)
                    } catch (error: Throwable) { socket.close(); throw error }
                }
                server = opened
                hostingMatch = game.gameId
                if (table.state.value.game.gameId != hostingMatch || !table.state.value.game.profile.deathMayDie) {
                    stopHosting()
                    _state.update { it.copy(busy = false) }
                    return@launch
                }
                _state.update { it.copy(host = invitation, busy = false, preview = null, message = null) }
            } catch (error: Throwable) {
                opened?.close(); sessions = null
                if (error is CancellationException) throw error
                _state.update { it.copy(busy = false, message = "Couldn't start sharing. Check Wi-Fi and Nearby devices permission, then try again.") }
            }
        }
    }

    fun stopHosting() {
        sessions = null
        hostingMatch = null
        server?.close(); server = null
        _state.update { it.copy(host = null, guests = emptyList(), message = "Sharing stopped. The local game is still on this device.") }
    }
    fun revoke(guest: LanGuest) {
        sessions?.revoke(guest.token)
        _state.update { it.copy(guests = sessions?.guests.orEmpty()) }
    }

    fun preview(text: String) {
        if (_state.value.busy || token != null || server != null) return
        val parsed = runCatching { LanInvite.parse(text) }.getOrNull()
        if (parsed == null) { _state.update { it.copy(message = "Scan or paste a complete TurnHub shared-game invitation.") }; return }
        invite = parsed; joinRequest = null
        _state.update { it.copy(busy = true, preview = null, message = null) }
        viewModelScope.launch {
            try {
                val reply = request(parsed, base(parsed, "preview").put("key", parsed.key))
                require(reply.text("code") == "ok")
                val game = readGame(reply)
                _state.update { it.copy(preview = game, busy = false) }
            } catch (error: Exception) {
                if (error is CancellationException) throw error
                _state.update { it.copy(busy = false, message = "Couldn't reach this game. Check the invitation, Wi-Fi and Nearby devices permission.") }
            }
        }
    }

    fun join(role: String, player: Int) {
        val invitation = invite ?: return
        if (_state.value.busy || _state.value.preview == null) return
        val body = joinRequest?.takeIf { it.optString("role") == role && it.optInt("player") == player }
            ?: base(invitation, "join").put("key", invitation.key).put("role", role).put("player", player)
                .put("joinId", UUID.randomUUID().toString()).also { joinRequest = it }
        _state.update { it.copy(busy = true, message = null) }
        viewModelScope.launch {
            try {
                val reply = request(invitation, body)
                if (reply.text("code") != "ok") {
                    _state.update { it.copy(busy = false, message = if (reply.text("code") == "claimed")
                        "That investigator is already controlled by another device. Ask the host to release it." else "Couldn't join. Ask the host for a new invitation.") }
                    return@launch
                }
                val game = readGame(reply)
                token = reply.text("token")
                generation++
                _state.update { it.copy(remote = game, role = role, player = player, preview = null, online = true, busy = false, message = null) }
                startPolling(generation)
            } catch (error: Exception) {
                if (error is CancellationException) throw error
                _state.update { it.copy(busy = false, message = "Connection interrupted. Try the same join option again.") }
            }
        }
    }

    private fun startPolling(epoch: Int) {
        polling?.cancel()
        polling = viewModelScope.launch {
            while (isActive && epoch == generation) {
                delay(1_000)
                val invitation = invite ?: break
                val credential = token ?: break
                try {
                    val reply = request(invitation, base(invitation, "poll").put("token", credential))
                    if (epoch == generation) receive(reply)
                } catch (error: Exception) {
                    if (error is CancellationException) throw error
                    if (epoch == generation) _state.update { it.copy(online = false, message = "Connection lost. Showing the last update; controls wait for the host.") }
                }
            }
        }
    }

    fun act(kind: String, value: Int = 0, slot: Int = 0) {
        val state = _state.value
        val game = state.remote ?: return
        val invitation = invite ?: return
        if (!state.online || state.busy || pending != null || state.role != "investigator") return
        pending = base(invitation, "command").put("token", token).put("actionId", UUID.randomUUID().toString())
            .put("match", game.gameId).put("revision", game.revision).put("kind", kind).put("value", value).put("slot", slot)
        retry()
    }

    fun retry() {
        val body = pending ?: return
        val invitation = invite ?: return
        if (_state.value.busy) return
        val epoch = generation
        _state.update { it.copy(busy = true, uncertain = true, message = null) }
        viewModelScope.launch {
            try {
                val reply = request(invitation, body)
                if (epoch != generation) return@launch
                if (reply.text("code") != "unavailable") pending = null
                receive(reply)
                _state.update { it.copy(busy = false, uncertain = pending != null) }
            } catch (error: Exception) {
                if (error is CancellationException) throw error
                if (epoch == generation) _state.update { it.copy(busy = false, online = false, uncertain = true,
                    message = "The reply was lost. Retry the same action to find out whether the host saved it.") }
            }
        }
    }

    fun leave() {
        val invitation = invite
        val credential = token
        generation++; polling?.cancel(); polling = null
        token = null; pending = null; invite = null; joinRequest = null
        _state.value = LanUiState()
        if (invitation != null && credential != null) viewModelScope.launch {
            runCatching { request(invitation, base(invitation, "leave").put("token", credential)) }
        }
    }

    private fun receive(reply: JSONObject) {
        val code = reply.text("code")
        val online = code in setOf("ok", "stale", "rejected")
        val game = if (reply.has("game")) readGame(reply) else null
        _state.update { before -> before.copy(
            remote = game?.takeIf { before.remote == null || it.revision >= before.remote.revision } ?: before.remote,
            online = online,
            message = when (code) {
                "ok" -> if (before.uncertain) before.message else null
                "stale" -> "The table changed before this edit arrived. Review the latest values and try again."
                "rejected" -> "That change isn't available in the current game state."
                "unavailable" -> "The host is away or unable to save. Controls wait until it returns."
                "renew" -> "Rejoin to continue this investigator session."
                else -> "This session has ended or access was removed. Ask the host for a new invitation."
            },
        ) }
    }

    private suspend fun request(invitation: LanInvite, body: JSONObject): JSONObject = exchange.withLock {
        withContext(Dispatchers.IO) {
            val wifi = currentLanWifi(context)
            lanExchange(invitation, body) { wifi.network.bindSocket(it) }
        }
    }
    private fun base(invitation: LanInvite, op: String) = JSONObject().put("v", LAN_VERSION).put("session", invitation.session).put("op", op)
    private fun readGame(reply: JSONObject): StandaloneGame = StandaloneTable.gameFromJson(reply.getJSONObject("game")).also {
        require(it.profile.deathMayDie && it.gameId.isNotBlank() && it.players.size in 1..10)
    }
    override fun onCleared() { sessions = null; server?.close(); super.onCleared() }
}
