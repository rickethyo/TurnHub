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
internal data class LanHosting(val code: String? = null, val qr: String? = null)

internal data class RoomDisplay(val id: String, val name: String, val upright: Boolean, val detail: String = "", val public: Boolean = false, val view: String? = null)

internal data class RoomProfile(val id: String, val name: String, val hasPin: Boolean)

internal data class LanUiState(
    val host: LanHosting? = null,
    val guests: List<LanGuest> = emptyList(),
    val preview: StandaloneGame? = null,
    val remote: StandaloneGame? = null,
    val role: String? = null,
    val player: Int = 0,
    val online: Boolean = false,
    val resumable: Boolean = false,
    val reconnecting: Boolean = false,
    val master: Boolean = false,
    val view: String = "personal",
    val directory: List<RoomProfile> = emptyList(),
    val busy: Boolean = false,
    val uncertain: Boolean = false,
    val message: String? = null,
    /** The host's public look for each seat, by player index, so guest panels match the table. */
    val looks: List<LocalPersonalization?> = emptyList(),
    val looksId: String = "",
    val displayAllUpright: Boolean = false,
    val displayId: String = "host",
    val displayTargets: List<RoomDisplay> = emptyList(),
    val displayView: String? = null,
    val planarRollFeedback: PlanarRollFeedback? = null,
) {
    /** Seated or displaying a shared game. */
    val joined: Boolean get() = reconnecting || remote != null
}

internal class LanViewModel(
    context: Context,
    private val table: StandaloneTable,
    private val hostingAccess: LanHostingAccess = LanHostingAccess { true },
) : ViewModel() {
    private val context = context.applicationContext
    private val _state = MutableStateFlow(LanUiState())
    val state = _state.asStateFlow()
    private var server: LanServer? = null
    private var advertiser: LanAdvertiser? = null
    private var room: LanRoom? = null
    private val store = LanRoomStore(this.context)
    private var hostRecord: JSONObject? = store.read("host")
    private var foreground = true
    private var invite: LanInvite? = null
    private var token: String? = null
    private var pending: JSONObject? = null
    private var joinRequest: JSONObject? = null
    private var joinChoice = ""
    private var polling: Job? = null
    private var generation = 0
    private val exchange = Mutex()
    private var commandEpoch = ""
    private data class QueuedAction(val match: String, val epoch: String, val kind: String, val value: Int,
        val slot: Int, val source: Int, val target: Int)
    private val queued = ArrayDeque<QueuedAction>()
    private var restoreAttempted = false

    init {
        _state.update { it.copy(resumable = hostRecord != null || store.read("guest") != null) }
    }
    fun setForeground(value: Boolean) {
        foreground = value
        if (value && !restoreAttempted) { restoreAttempted = true; resumeRoom() }
    }
    val holdsConnection get() = server != null || token != null
    fun permissionDenied() { _state.update { it.copy(message = "Allow Nearby devices in Android settings to host or join over Wi-Fi.") } }
    fun joinNewProfile(name: String) {
        if (!table.createAccount(name)) { unsupportedAction(); return }
        val added = table.state.value.library.players.lastOrNull() ?: return
        join("player", 0, localId = added.localId)
    }
    fun savedPlayers() = table.state.value.library.players
    fun hostPlanesAvailable(): Boolean = table.state.value.game.planechase.enabled
    fun chooseView(view: String) {
        _state.update { it.copy(view = view) }
        saveGuest()
    }
    private fun saveGuest(): Boolean {
        val invitation = invite ?: return false
        val credential = token ?: return false
        return store.write("guest", JSONObject().put("invite", invitation.encode()).put("token", credential)
            .put("role", _state.value.role).put("view", _state.value.view))
    }
    fun resumeRoom() {
        if (_state.value.busy || server != null || token != null) return
        val host = hostRecord
        if (host != null) { startHosting(host.optBoolean("qrOnly")); return }
        val saved = store.read("guest") ?: return
        invite = runCatching { LanInvite.parse(saved.text("invite")) }.getOrNull() ?: return
        token = saved.text("token")
        _state.update { it.copy(role = saved.optString("role"), view = saved.optString("view", "personal"),
            reconnecting = true, resumable = true, message = "Reconnecting to your room…") }
        generation++
        startPolling(generation)
    }
    fun startHosting(qrOnly: Boolean) {
        if (_state.value.busy || token != null || server != null) return
        if (!hostingAccess.allowed()) return
        if (table.state.value.storageProblem != null) return
        val record = hostRecord ?: JSONObject().put("session", randomLanSecret(16))
            .put("key", if (qrOnly) randomLanSecret() else LanJoinCode.generate()).put("ownerKey", randomLanSecret())
            .put("qrOnly", qrOnly)
        _state.update { it.copy(busy = true, message = null) }
        viewModelScope.launch {
            var opened: LanServer? = null
            try {
                record.put("hostDeviceName", android.os.Build.MODEL.take(64))
                check(store.write("host", record))
                hostRecord = record
                val session = record.text("session")
                val key = record.text("key")
                fun localCore() = LanSessions(session, key, { table.state.value.game }, table::applyLanAction,
                    unavailable = { (!foreground && !AtlasLinkHoldService.active) || table.state.value.storageProblem != null },
                    looks = { val state = table.state.value; val looks = state.library.players.associate { it.localId to it.personalization }
                        state.game.players.map { looks[it.localId] } })
                fun baseRequest(op: String) = JSONObject().put("v", LAN_VERSION).put("session", session).put("op", op).put("key", key)
                suspend fun snapshot(): JSONObject = localCore().handle(baseRequest("preview"))
                val core = LanRoom(record, save = { next -> store.write("host", next) },
                    planesAvailable = { table.state.value.game.planechase.enabled },
                    planarPlayerAllowed = { player -> table.state.value.game.players.getOrNull(player - 1)?.eliminated == false },
                    planarRollAllowed = { player ->
                        table.state.value.game.let { game -> game.state == com.turnhub.android.protocol.TableState.RUNNING &&
                            game.active?.let { player - 1 in game.mates(it) } == true }
                    }, snapshot = ::snapshot, resolveIdentity = { it }, seats = {
                        val state = table.state.value
                        state.game.players.mapIndexed { i, p ->
                            LanRoom.RoomSeat(state.library.players.firstOrNull { it.localId == p.localId }?.profileId
                                ?: p.profileId ?: "guest:${state.game.gameId}:${p.name}", i + 1, p.name)
                        }
                }, profiles = { table.roomProfiles() },
                    attach = { table.attachRoomProfile(it) },
                    command = { request, player ->
                        // Reuse the existing validators against the current seat, never a remembered name.
                        val role = if (player <= 0 && request.text("kind").startsWith(PLANAR_KIND)) "planes" else "player"
                        val join = baseRequest("join").put("role", role).put("player", if (role == "planes") 0 else player).put("joinId", UUID.randomUUID().toString())
                        val local = localCore()
                        val joined = local.handle(join)
                        if (joined.optString("code") != "ok") joined else {
                            val action = JSONObject(request.toString()).put("token", joined.text("token"))
                            local.handle(action)
                        }
                    })
                room = core
                val invitation = withContext(Dispatchers.IO) {
                    val wifi = currentLanWifi(context)
                    val (tls, pin) = lanServerContext()
                    val socket = tls.serverSocketFactory.createServerSocket()
                    try {
                        socket.bind(InetSocketAddress(wifi.address, 0), 8)
                        val listening = LanServer(socket) { request -> runBlocking(Dispatchers.Main.immediate) {
                            if (!foreground) table.tick()
                            val answer = if (room === core) core.handle(request) else baseRequest("ended").put("code", "ended")
                            _state.update { it.copy(guests = core.guests, displayAllUpright = core.displayAllUpright("host"), displayId = "host", displayTargets = readDisplays(core.displayTargets())) }
                            answer
                        } }
                        opened = listening
                        // Discovery carries no secret, including for QR-only rooms. Reconnect pins the saved certificate.
                        advertiser = LanAdvertiser(context, wifi.network, listening.port, session, pin)
                        LanInvite(wifi.address.hostAddress!!, listening.port, pin, session, key)
                    } catch (error: Throwable) { socket.close(); throw error }
                }
                server = opened
                _state.update { it.copy(host = LanHosting(key.takeUnless { record.optBoolean("qrOnly") }, invitation.encode()),
                    guests = core.guests, displayAllUpright = core.displayAllUpright("host"), displayId = "host", displayTargets = readDisplays(core.displayTargets()), busy = false, preview = null, resumable = true) }
            } catch (error: Exception) {
                opened?.close(); room = null; advertiser?.close(); advertiser = null
                if (error is CancellationException) throw error
                _state.update { it.copy(busy = false, message = "Couldn't resume hosting. Check Wi-Fi, storage and Nearby devices permission; your room is saved.") }
            }
        }
    }
    fun atTable(summary: com.turnhub.android.domain.TableSummary): Boolean =
        _state.value.remote?.let { it.gameId == summary.bootId } ?: false
    fun unsupportedAction() { _state.update { it.copy(message = "Use the host for this game setup or decision.") } }
    fun stopHosting() {
        if (hostRecord == null) { leave(); return }
        if (!store.write("host", null)) { _state.update { it.copy(message = "Couldn't end the saved room. Try again.") }; return }
        room = null; hostRecord = null
        advertiser?.close(); advertiser = null; server?.close(); server = null
        _state.update { it.copy(host = null, guests = emptyList(), resumable = false, message = "Room ended. The game is kept.") }
    }
    fun revoke(guest: LanGuest) {
        val result = runCatching { room?.revoke(guest.token) }
        _state.update { it.copy(guests = room?.guests.orEmpty(), message = if (result.isFailure) "Couldn't save device removal. Try again." else null) }
    }
    fun grantMaster(guest: LanGuest) {
        val result = runCatching { room?.grant(guest.token, !guest.master) }
        _state.update { it.copy(guests = room?.guests.orEmpty(), message = if (result.isFailure) "Couldn't save this permission. Try again." else null) }
    }
    /** [text] is a typed join code, or the invitation text a QR scan read. */
    fun preview(text: String) {
        if (_state.value.busy || token != null || server != null) return
        if (hostRecord != null) { _state.update { it.copy(message = "Resume or end your saved room before joining another.") }; return }
        val code = LanJoinCode.normalize(text)
        val scanned = if (code == null) runCatching { LanInvite.parse(text) }.getOrNull() else null
        if (code == null && scanned == null) { _state.update { it.copy(message = "Enter the host's join code, like abc-123.") }; return }
        invite = null; joinRequest = null
        _state.update { it.copy(busy = true, preview = null, message = null) }
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
                        _state.update { it.copy(preview = readGame(reply), directory = readDirectory(reply), busy = false) }
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

    fun join(role: String, player: Int, profileId: String? = null, pin: String = "", localId: String? = null, sourcePin: String? = null) {
        val invitation = invite ?: return
        if (_state.value.busy || _state.value.preview == null) return
        val own = savedPlayers().firstOrNull { it.localId == localId }
        val targetId = profileId
        val choice = listOf(role, player, targetId, localId).joinToString("|")
        val body = joinRequest?.takeIf { joinChoice == choice }
            ?: base(invitation, "join").put("key", invitation.key).put("role", role).put("player", player)
                .put("joinId", UUID.randomUUID().toString()).put("deviceName", android.os.Build.MODEL.take(64)).also { joinRequest = it; joinChoice = choice }
        body.put("pin", pin)
        if (targetId != null) body.put("profileId", targetId)
        if (own != null) {
            if (own.credential?.verifies(sourcePin ?: pin) == false) { _state.update { it.copy(message = "Enter your profile PIN.") }; return }
            body.put("profile", JSONObject().put("profileId", own.profileId).put("name", own.name)
                .put("createdAtMs", own.createdAtMs).put("activity", own.lastActivityMs).put("look", own.personalization.toJson())
                .put("credential", own.credential?.let { JSONObject().put("salt", it.salt).put("hash", it.hash) } ?: JSONObject.NULL))
        }
        _state.update { it.copy(busy = true, message = null) }
        viewModelScope.launch {
            try {
                val reply = request(invitation, body)
                if (reply.text("code") != "ok") {
                    _state.update { it.copy(busy = false, message = if (reply.text("code") == "claimed")
                        "That player is already controlled by another device." else if (reply.text("code") == "pin-required") "Enter the profile PIN to authorize this link." else "Couldn't join. Check the profile and room invitation.") }
                    return@launch
                }
                val game = readGame(reply)
                token = reply.text("token")
                commandEpoch = reply.optString("epoch")
                if (own != null) {
                    reply.optJSONObject("profileLink")?.let { link ->
                        table.linkAccount(own.localId, link.getString("atlasId"), link.getString("profileId"), link.optString("linkKey").takeIf { it.isNotBlank() })
                    }
                    reply.optJSONObject("profile")?.takeIf { it.optString("profileId") == own.profileId }?.let { synced ->
                        val look = LocalPersonalization.fromJson(synced.getJSONObject("look"))
                        table.applySyncedProfile(own.localId, synced.getString("name"), look.color, look.avatar, look.reduceMotion, synced.optLong("activity"))
                        table.keepProfileCreationTime(own.localId, synced.optLong("createdAtMs"))
                    }
                }
                joinRequest = null
                generation++
                _state.update { it.copy(remote = game, role = role, player = reply.optInt("player", player), preview = null,
                    online = true, busy = false, master = reply.optBoolean("master"),
                    view = if (role == "planes") "planes" else if (role in setOf("display", "table")) "table" else "personal",
                    displayView = reply.optString("displayView").takeIf { it in setOf("table", "planes") },
                    displayId = reply.optString("displayId", "host"), displayAllUpright = reply.optBoolean("displayAllUpright"),
                    displayTargets = reply.optJSONArray("displayTargets")?.let(::readDisplays).orEmpty(),
                    message = null).withLooks(reply) }
                val remembered = saveGuest()
                startPolling(generation)
                if (!remembered) _state.update { it.copy(message = "Joined, but this device couldn't save reconnect access. Keep the app open.") }
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
                    val reply = request(invitation, base(invitation, "poll").put("deviceName", android.os.Build.MODEL.take(64)).put("token", credential).put("looksId", _state.value.looksId))
                    if (epoch == generation) receive(reply)
                } catch (error: Exception) {
                    if (error is CancellationException) throw error
                    if (epoch == generation) {
                        _state.update { it.copy(online = false, message = "Connection lost. Reconnecting to your room…") }
                        rediscover()
                    }
                }
            }
        }
    }

    /** Absolute, idempotent presentation preference; never a gameplay action or seat grant. */
    fun setDisplayAllUpright(target: String, value: Boolean) = setDisplaySetting(target, value, null)

    fun setDisplayView(target: String, view: String) = setDisplaySetting(target, null, view)

    private fun setDisplaySetting(target: String, value: Boolean?, view: String?) {
        val core = room
        if (core != null) {
            val saved = runCatching {
                if (view != null) core.setDisplayView(target, view) else core.setDisplayAllUpright(target, requireNotNull(value))
            }
            _state.update { it.copy(displayAllUpright = core.displayAllUpright("host"), displayId = "host", displayTargets = readDisplays(core.displayTargets()),
                message = if (saved.isFailure) "Couldn't save the display setting. Try again." else null) }
            return
        }
        val state = _state.value
        val invitation = invite ?: return
        if (!state.online || state.busy || state.uncertain || state.role !in setOf("player", "table") && !state.master &&
            !(view != null && target == state.displayId && state.role in setOf("display", "planes"))) return
        val g = generation
        _state.update { it.copy(busy = true) }
        viewModelScope.launch {
            try {
                val body = base(invitation, "presentation").put("token", token).put("displayTarget", target)
                if (view != null) body.put("displayView", view) else body.put("displayAllUpright", value)
                val reply = request(invitation, body)
                if (g == generation) receive(reply)
            } catch (e: CancellationException) { throw e }
              catch (_: Exception) {
                if (g == generation) _state.update { it.copy(message = "Display reply unavailable. Check the setting before trying again.") }
            } finally { if (g == generation) {
                _state.update { it.copy(busy = false) }
                drainActions()
            } }
        }
    }

    fun beginPlanarRoll(fromEffect: Boolean = false): String? {
        val current = _state.value
        val summary = current.remote?.toSummary(System.currentTimeMillis(), com.turnhub.android.domain.TableClock.nowMs()) ?: return null
        if (!planarRollAllowed(current, summary) || pending != null || invite == null) return null
        val id = UUID.randomUUID().toString()
        _state.update { it.copy(planarRollFeedback = PlanarRollFeedback(id, "pending")) }
        act("planar-roll", if (fromEffect) 1 else 0, actionId = id)
        return id
    }

    fun act(kind: String, value: Int = 0, slot: Int = 0, source: Int = 0, target: Int = 0, actionId: String? = null) {
        val state = _state.value
        val (match, revision) = state.remote?.let { it.gameId to it.revision } ?: return
        val invitation = invite ?: return
        if (!state.online || state.role !in setOf("player", "table") && !state.master) return
        if (state.busy || pending != null) {
            if (queued.size < 64) queued.addLast(QueuedAction(match, commandEpoch, kind, value, slot, source, target))
            else _state.update { it.copy(message = "Too many changes are waiting. Review the table before another edit.") }
            return
        }
        pending = base(invitation, "command").put("token", token).put("actionId", actionId ?: UUID.randomUUID().toString())
            .put("epoch", commandEpoch).put("target", target.takeIf { it > 0 } ?: state.player).put("match", match).put("revision", revision).put("kind", kind).put("value", value).put("slot", slot).put("source", source).put("looksId", state.looksId)
        retry()
    }

    fun retry() {
        val body = pending ?: return
        val invitation = invite ?: return
        if (_state.value.busy) return
        val epoch = generation
        _state.update { it.copy(busy = true, uncertain = true, message = null) }
        if (isPlanarRollKind(body.optString("kind"))) _state.update { it.copy(planarRollFeedback = PlanarRollFeedback(body.text("actionId"), "pending")) }
        viewModelScope.launch {
            try {
                val reply = request(invitation, body)
                if (epoch != generation) return@launch
                if (reply.text("code") != "unavailable") pending = null
                receive(reply)
                if (isPlanarRollKind(body.optString("kind"))) _state.update {
                    it.copy(planarRollFeedback = confirmedPlanarFeedback(body.text("actionId"), reply))
                }
                _state.update { it.copy(busy = false, uncertain = pending != null) }
                if (pending == null) drainActions()
            } catch (error: Exception) {
                if (error is CancellationException) throw error
                if (epoch == generation) _state.update { it.copy(busy = false, online = false, uncertain = true,
                    planarRollFeedback = if (isPlanarRollKind(body.optString("kind"))) PlanarRollFeedback(body.text("actionId"), "uncertain") else it.planarRollFeedback,
                    message = "The reply was lost. Retry the same action to find out whether the host saved it.") }
            }
        }
    }

    private fun drainActions() {
        if (queued.isEmpty() || pending != null || _state.value.busy || !_state.value.online) return
        val next = queued.removeFirst()
        val current = _state.value.remote?.gameId
        if (current != next.match || commandEpoch != next.epoch) {
            queued.clear()
            _state.update { it.copy(message = "The game or host changed. Waiting edits were not replayed; review current values.") }
            return
        }
        act(next.kind, next.value, next.slot, next.source, next.target)
    }
    fun leave() {
        queued.clear()
        val saved = store.read("guest")
        val invitation = invite ?: saved?.optString("invite")?.let { runCatching { LanInvite.parse(it) }.getOrNull() }
        val credential = token ?: saved?.optString("token")
        generation++; polling?.cancel(); polling = null
        store.write("guest", null)
        token = null; pending = null; invite = null; joinRequest = null
        _state.value = LanUiState()
        if (invitation != null && credential != null) viewModelScope.launch {
            runCatching { request(invitation, base(invitation, "leave").put("token", credential)) }
        }
    }

    private fun receive(reply: JSONObject) {
        val code = reply.text("code")
        if (reply.has("epoch")) commandEpoch = reply.text("epoch")
        if (code == "unauthorized" || code == "ended") {
            store.write("guest", null)
            generation++; polling?.cancel(); token = null; pending = null; queued.clear()
            _state.value = LanUiState(message = "Room ended or this device was removed. Join with the room code again.")
            return
        }
        if (code == "released") {
            // The host removed this player from its lobby: choose a seat again with the same access.
            generation++; polling?.cancel(); polling = null; token = null; pending = null; joinRequest = null
            _state.update { it.copy(remote = null, role = null, player = 0, online = false, uncertain = false,
                preview = if (reply.has("game")) readGame(reply) else it.remote,
                message = "Your player left the host's table. Choose a seat again.") }
            return
        }
        val online = code in setOf("ok", "stale", "rejected", "view-only", "forbidden", "interrupted", "renew")
        val game = if (reply.has("game")) readGame(reply) else null
        _state.update { before -> before.copy(
            player = reply.optInt("player", before.player),
            role = reply.optString("role", before.role ?: "display"),
            master = reply.optBoolean("master", before.master),
            displayAllUpright = reply.optBoolean("displayAllUpright", before.displayAllUpright),
            displayView = reply.optString("displayView").takeIf { it in setOf("table", "planes") },
            displayId = reply.optString("displayId", before.displayId),
            displayTargets = reply.optJSONArray("displayTargets")?.let(::readDisplays) ?: before.displayTargets,
            reconnecting = before.reconnecting && game == null,
            directory = if (reply.has("directory")) readDirectory(reply) else before.directory,
            remote = game?.takeIf { before.remote == null || it.gameId != before.remote.gameId || it.revision >= before.remote.revision } ?: before.remote,
            online = online,
            looks = if (reply.has("looks")) readLooks(reply) else before.looks,
            looksId = if (reply.has("looks")) reply.optString("looksId") else before.looksId,
            message = when (code) {
                "ok" -> if (before.uncertain) before.message else null
                "view-only" -> null
                "stale" -> "The table changed before this edit arrived. Review the latest values and try again."
                "rejected" -> "That change isn't available in the current game state."
                "unavailable" -> "The host is away or unable to save. Controls wait until it returns."
                "interrupted" -> "The host or connection restarted. Review the current values; your previous edit was not replayed."
                "forbidden" -> "This view is read only. Game master permission is needed for that action."
                "renew" -> "Connection renewed without signing in. The last edit was not applied; review it before trying again."
                else -> "This session has ended or access was removed. Ask the host for a new invitation."
            },
        ) }
    }

    private suspend fun rediscover() {
        val old = invite ?: return
        runCatching {
            val hosts = withContext(Dispatchers.IO) { discoverLanHosts(context, currentLanWifi(context)) }
            hosts.firstOrNull { it.session == old.session && it.pin == old.pin }?.let {
                invite = old.copy(address = it.address, port = it.port)
                saveGuest()
            }
        }
    }
    private fun readDisplays(rows: org.json.JSONArray): List<RoomDisplay> = (0 until rows.length()).map { i ->
        val row = rows.getJSONObject(i)
        RoomDisplay(row.text("id"), row.text("name"), row.getBoolean("upright"), row.optString("detail"), row.optBoolean("public"), row.optString("view").takeIf { it in setOf("table", "planes") })
    }

    private fun readDirectory(reply: JSONObject): List<RoomProfile> {
        val array = reply.optJSONArray("directory") ?: return emptyList()
        return (0 until minOf(array.length(), 128)).map { array.getJSONObject(it).let { p ->
            RoomProfile(p.text("profileId"), p.text("name"), p.optBoolean("hasPin"))
        } }
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
    private fun readGame(reply: JSONObject): StandaloneGame = StandaloneTable.gameFromJson(reply.getJSONObject("game")).also {
        require(it.profile.lanSupported && it.players.size in 0..10)
    }
    override fun onCleared() { room = null; advertiser?.close(); server?.close(); super.onCleared() }
}
