package com.turnhub.android.lan

import android.content.Context
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.turnhub.android.data.AtlasLinkHoldService
import com.turnhub.android.standalone.LocalPersonalization
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

/**
 * What a host shows its table: the join code in standard joining, or the QR invitation in
 * QR-only joining (the device's own security setting). Guests must use whichever it shows.
 */
internal data class LanHosting(val code: String? = null, val qr: String? = null,
    /** Relaying the connected Atlas game rather than sharing this device's own. */
    val atlas: Boolean = false)

internal data class LanUiState(
    val host: LanHosting? = null,
    val guests: List<LanGuest> = emptyList(),
    val preview: StandaloneGame? = null,
    val remote: StandaloneGame? = null,
    val role: String? = null,
    val player: Int = 0,
    val online: Boolean = false,
    val busy: Boolean = false,
    val uncertain: Boolean = false,
    val message: String? = null,
    /** The host's public look for each seat, by player index, so guest panels match the table. */
    val looks: List<LocalPersonalization?> = emptyList(),
    val looksId: String = "",
    /** The Atlas game a hub relays, in place of [preview] or [remote] (Atlas decides it, not the hub). */
    val previewAtlas: com.turnhub.android.domain.TableSummary? = null,
    val atlas: com.turnhub.android.domain.TableSummary? = null,
) {
    /** Seated or displaying a shared game, whichever device or Atlas runs it. */
    val joined: Boolean get() = remote != null || atlas != null
}

internal class LanViewModel(
    context: Context,
    private val table: StandaloneTable,
    private val hostingAccess: LanHostingAccess = LanHostingAccess { true },
    /** The connected Atlas game this device may relay to guests; null without one. */
    private val atlasLink: LanAtlasLink? = null,
) : ViewModel() {
    private val context = context.applicationContext
    private val _state = MutableStateFlow(LanUiState())
    val state = _state.asStateFlow()
    private var server: LanServer? = null
    private var hostingProfile: com.turnhub.android.protocol.GameProfile? = null
    private var advertiser: LanAdvertiser? = null
    private var sessions: LanSessions? = null
    private var atlasSessions: LanAtlasSessions? = null
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
                if (server != null && atlasSessions == null && local.game.profile != hostingProfile) stopHosting()
            }
        }
    }

    fun setForeground(value: Boolean) { foreground = value }
    /** Hosting or seated as a guest: leaving the screen should hold the Wi-Fi link. */
    val holdsConnection get() = server != null || token != null
    fun permissionDenied() { _state.update { it.copy(message = "Allow Nearby devices in Android settings to host or join over Wi-Fi.") } }

    /** Whether this device can relay its connected Atlas game (tablet mode lets guests' taps through). */
    fun atlasRelayable(): Boolean = atlasLink?.summary()?.stateWire != null

    /**
     * [qrOnly] is this device's join security: true makes guests scan its QR instead of typing a code.
     * [fromAtlas] relays the connected Atlas game instead of sharing this device's own.
     */
    fun startHosting(qrOnly: Boolean, fromAtlas: Boolean = false) {
        if (_state.value.busy || token != null || server != null) return
        if (fromAtlas) { startAtlasHosting(qrOnly); return }
        if (table.state.value.storageProblem != null) {
            _state.update { it.copy(message = "Resolve the local game storage problem before hosting.") }; return
        }
        if (!hostingAccess.allowed()) { _state.update { it.copy(message = "Hosting is not available for this account.") }; return }
        val game = table.state.value.game
        if (!game.profile.lanSupported) {
            _state.update { it.copy(message = "Choose a TCG or Death May Die game on this device before sharing it.") }; return
        }
        _state.update { it.copy(busy = true, message = null) }
        viewModelScope.launch {
            var opened: LanServer? = null
            try {
                val session = randomLanSecret(16)
                val code = if (qrOnly) null else LanJoinCode.generate()
                val key = code ?: randomLanSecret()
                val core = LanSessions(session, key, { table.state.value.game }, table::applyLanAction,
                    // A backgrounded host keeps serving while the link hold runs.
                    unavailable = { (!foreground && !AtlasLinkHoldService.active) || table.state.value.storageProblem != null },
                    looks = {
                        val state = table.state.value
                        val saved = state.library.players.associate { it.localId to it.personalization }
                        state.game.players.map { saved[it.localId] }
                    })
                sessions = core
                val invitation = withContext(Dispatchers.IO) {
                    val wifi = currentLanWifi(context)
                    val (tls, pin) = lanServerContext()
                    val socket = tls.serverSocketFactory.createServerSocket()
                    try {
                        socket.bind(InetSocketAddress(wifi.address, 0), 8)
                        val listening = LanServer(socket) { request ->
                            runBlocking(Dispatchers.Main.immediate) {
                                // The screen's turn-timer tick stops in the background; catch up first.
                                if (!foreground) table.tick()
                                val reply = if (sessions === core) core.handle(request) else JSONObject()
                                    .put("v", LAN_VERSION).put("session", session).put("code", "ended")
                                _state.update { it.copy(guests = sessions?.guests.orEmpty()) }
                                reply
                            }
                        }
                        opened = listening
                        if (!qrOnly) advertiser = LanAdvertiser(context, wifi.network, listening.port, session, pin)
                        LanInvite(wifi.address.hostAddress!!, listening.port, pin, session, key)
                    } catch (error: Throwable) { socket.close(); throw error }
                }
                hostingProfile = game.profile
                server = opened
                if (table.state.value.game.profile != hostingProfile) {
                    stopHosting()
                    _state.update { it.copy(busy = false) }
                    return@launch
                }
                _state.update { it.copy(host = LanHosting(code, invitation.encode().takeIf { qrOnly }), busy = false, preview = null, message = null) }
            } catch (error: Throwable) {
                opened?.close(); sessions = null; advertiser?.close(); advertiser = null
                if (error is CancellationException) throw error
                _state.update { it.copy(busy = false, message = "Couldn't start sharing. Check Wi-Fi and Nearby devices permission, then try again.") }
            }
        }
    }

    /** Opens the same Wi-Fi invitation as device hosting, answered by [LanAtlasSessions]. */
    private fun startAtlasHosting(qrOnly: Boolean) {
        val link = atlasLink
        if (link == null || !atlasRelayable()) {
            _state.update { it.copy(message = "Connect to the Atlas table first, then share its game.") }; return
        }
        if (!hostingAccess.allowed()) { _state.update { it.copy(message = "Hosting is not available for this account.") }; return }
        _state.update { it.copy(busy = true, message = null) }
        viewModelScope.launch {
            var opened: LanServer? = null
            try {
                val session = randomLanSecret(16)
                val code = if (qrOnly) null else LanJoinCode.generate()
                val key = code ?: randomLanSecret()
                val core = LanAtlasSessions(session, key, link,
                    unavailable = { !foreground && !AtlasLinkHoldService.active })
                atlasSessions = core
                val invitation = withContext(Dispatchers.IO) {
                    val wifi = currentLanWifi(context)
                    val (tls, pin) = lanServerContext()
                    val socket = tls.serverSocketFactory.createServerSocket()
                    try {
                        socket.bind(InetSocketAddress(wifi.address, 0), 8)
                        val listening = LanServer(socket) { request ->
                            runBlocking(Dispatchers.Main.immediate) {
                                val reply = if (atlasSessions === core) core.handle(request) else JSONObject()
                                    .put("v", LAN_VERSION).put("session", session).put("code", "ended")
                                _state.update { it.copy(guests = atlasSessions?.guests.orEmpty()) }
                                reply
                            }
                        }
                        opened = listening
                        if (!qrOnly) advertiser = LanAdvertiser(context, wifi.network, listening.port, session, pin)
                        LanInvite(wifi.address.hostAddress!!, listening.port, pin, session, key)
                    } catch (error: Throwable) { socket.close(); throw error }
                }
                server = opened
                _state.update { it.copy(host = LanHosting(code, invitation.encode().takeIf { qrOnly }, atlas = true), busy = false,
                    preview = null, previewAtlas = null, message = null) }
            } catch (error: Throwable) {
                opened?.close(); atlasSessions = null; advertiser?.close(); advertiser = null
                if (error is CancellationException) throw error
                _state.update { it.copy(busy = false, message = "Couldn't start sharing. Check Wi-Fi and Nearby devices permission, then try again.") }
            }
        }
    }

    fun stopHosting() {
        sessions = null
        val relayed = atlasSessions != null
        atlasSessions = null
        hostingProfile = null
        advertiser?.close(); advertiser = null
        server?.close(); server = null
        _state.update { it.copy(host = null, guests = emptyList(), message = if (relayed) "Sharing stopped. The game carries on at Atlas."
            else "Sharing stopped. The local game is still on this device.") }
    }
    fun revoke(guest: LanGuest) {
        sessions?.revoke(guest.token)
        atlasSessions?.revoke(guest.token)
        _state.update { it.copy(guests = sessions?.guests ?: atlasSessions?.guests.orEmpty()) }
    }

    /** [text] is a typed join code, or the invitation text a QR scan read. */
    fun preview(text: String) {
        if (_state.value.busy || token != null || server != null) return
        val code = LanJoinCode.normalize(text)
        val scanned = if (code == null) runCatching { LanInvite.parse(text) }.getOrNull() else null
        if (code == null && scanned == null) { _state.update { it.copy(message = "Enter the host's join code, like abc-123.") }; return }
        invite = null; joinRequest = null
        _state.update { it.copy(busy = true, preview = null, previewAtlas = null, message = null) }
        viewModelScope.launch {
            try {
                val candidates = scanned?.let(::listOf) ?: withContext(Dispatchers.IO) {
                    discoverLanHosts(context, currentLanWifi(context)).map { LanInvite(it.address, it.port, it.pin, it.session, code!!) }
                }
                var answer: String? = null
                for (candidate in candidates) {
                    val reply = runCatching { request(candidate, base(candidate, "preview").put("key", candidate.key)) }.getOrNull() ?: continue
                    answer = reply.text("code")
                    if (answer == "ok") {
                        invite = candidate
                        _state.update { if (reply.has("atlas")) it.copy(previewAtlas = readAtlas(reply), busy = false)
                            else it.copy(preview = readGame(reply), busy = false) }
                        return@launch
                    }
                    if (answer == "locked") break
                }
                _state.update { it.copy(busy = false, message = when {
                    answer == "locked" -> "Too many wrong codes were tried. Wait a minute, then enter the code again."
                    scanned != null -> "Couldn't reach this game. Check the Wi-Fi and Nearby devices permission, or scan the code again."
                    candidates.isEmpty() -> "No shared game found on this Wi-Fi. Check the host is sharing and both devices use the same Wi-Fi. If the host shows a QR code, scan it instead."
                    else -> "No game on this Wi-Fi uses that code. Check it, or scan the host's QR code if it shows one."
                }) }
            } catch (error: Exception) {
                if (error is CancellationException) throw error
                _state.update { it.copy(busy = false, message = "Couldn't search this Wi-Fi. Check the Wi-Fi and Nearby devices permission.") }
            }
        }
    }

    fun join(role: String, player: Int) {
        val invitation = invite ?: return
        if (_state.value.busy || _state.value.preview == null && _state.value.previewAtlas == null) return
        val body = joinRequest?.takeIf { it.optString("role") == role && it.optInt("player") == player }
            ?: base(invitation, "join").put("key", invitation.key).put("role", role).put("player", player)
                .put("joinId", UUID.randomUUID().toString()).also { joinRequest = it }
        _state.update { it.copy(busy = true, message = null) }
        viewModelScope.launch {
            try {
                val reply = request(invitation, body)
                if (reply.text("code") != "ok") {
                    _state.update { it.copy(busy = false, message = if (reply.text("code") == "claimed")
                        "That player is already controlled by another device. Ask the host to release it." else "Couldn't join. Ask the host for a new invitation.") }
                    return@launch
                }
                val atlas = if (reply.has("atlas")) readAtlas(reply) else null
                val game = if (atlas == null) readGame(reply) else null
                token = reply.text("token")
                generation++
                _state.update { it.copy(remote = game, atlas = atlas, role = role, player = reply.optInt("player", player), preview = null,
                    previewAtlas = null, online = true, busy = false, message = viewOnlyNote(reply)).withLooks(reply) }
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
                    val reply = request(invitation, base(invitation, "poll").put("token", credential).put("looksId", _state.value.looksId))
                    if (epoch == generation) receive(reply)
                } catch (error: Exception) {
                    if (error is CancellationException) throw error
                    if (epoch == generation) _state.update { it.copy(online = false, message = "Connection lost. Showing the last update; controls wait for the host.") }
                }
            }
        }
    }

    fun act(kind: String, value: Int = 0, slot: Int = 0, source: Int = 0) {
        val state = _state.value
        val (match, revision) = state.remote?.let { it.gameId to it.revision }
            ?: state.atlas?.let { it.relayMatch() to it.revision } ?: return
        val invitation = invite ?: return
        if (!state.online || state.busy || pending != null || state.role !in setOf("investigator", "player", "planes")) return
        pending = base(invitation, "command").put("token", token).put("actionId", UUID.randomUUID().toString())
            .put("match", match).put("revision", revision).put("kind", kind).put("value", value).put("slot", slot).put("source", source).put("looksId", state.looksId)
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
        if (code == "released") {
            // The host removed this player from its lobby: choose a seat again with the same access.
            generation++; polling?.cancel(); polling = null; token = null; pending = null; joinRequest = null
            _state.update { it.copy(remote = null, atlas = null, role = null, player = 0, online = false, uncertain = false,
                preview = if (reply.has("game")) readGame(reply) else it.remote,
                previewAtlas = if (reply.has("atlas")) readAtlas(reply) else it.atlas,
                message = "Your player left the host's table. Choose a seat again.") }
            return
        }
        val online = code in setOf("ok", "stale", "rejected", "view-only")
        val game = if (reply.has("game")) readGame(reply) else null
        val atlas = if (reply.has("atlas")) runCatching { readAtlas(reply) }.getOrNull() else null
        _state.update { before -> before.copy(
            player = reply.optInt("player", before.player),
            remote = game?.takeIf { before.remote == null || it.revision >= before.remote.revision } ?: before.remote,
            // A new Atlas boot starts its revisions again; within one, never step back.
            atlas = atlas?.takeIf { before.atlas == null || it.bootId != before.atlas.bootId || it.revision >= before.atlas.revision } ?: before.atlas,
            online = online,
            looks = if (reply.has("looks")) readLooks(reply) else before.looks,
            looksId = if (reply.has("looks")) reply.optString("looksId") else before.looksId,
            message = when (code) {
                "ok" -> if (before.uncertain) before.message else viewOnlyNote(reply)
                "view-only" -> viewOnlyNote(reply)
                "stale" -> "The table changed before this edit arrived. Review the latest values and try again."
                "rejected" -> "That change isn't available in the current game state."
                "unavailable" -> "The host is away or unable to save. Controls wait until it returns."
                "renew" -> "Rejoin to continue this player session."
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
    private fun LanUiState.withLooks(reply: JSONObject) =
        if (reply.has("looks")) copy(looks = readLooks(reply), looksId = reply.optString("looksId")) else this
    /** A look the guest can't read is just the default panel; it never blocks the game. */
    private fun readLooks(reply: JSONObject): List<LocalPersonalization?> {
        val array = reply.optJSONArray("looks") ?: return emptyList()
        return (0 until minOf(array.length(), 10)).map { i ->
            array.optJSONObject(i)?.let { runCatching { LocalPersonalization.fromJson(it) }.getOrNull() }
        }
    }
    /** Atlas's own state, relayed by a hub; parsed with the same rules as a direct connection. */
    private fun readAtlas(reply: JSONObject): com.turnhub.android.domain.TableSummary =
        LanAtlasFrame.decode(reply.getJSONObject("atlas"), com.turnhub.android.domain.TableClock.nowMs())
    /** A hub that isn't the Atlas table's tablet can show the game, but taps can't reach Atlas. */
    private fun viewOnlyNote(reply: JSONObject): String? = if (reply.optBoolean("viewOnly"))
        "The host isn't the Atlas table's tablet yet, so this view is read only. Ask them to use their device as the table screen." else null
    private fun readGame(reply: JSONObject): StandaloneGame = StandaloneTable.gameFromJson(reply.getJSONObject("game")).also {
        require(it.profile.lanSupported && it.players.size in 0..10)
    }
    override fun onCleared() { sessions = null; atlasSessions = null; advertiser?.close(); server?.close(); super.onCleared() }
}
