package com.turnhub.android.data

import kotlinx.coroutines.NonCancellable
import kotlinx.coroutines.currentCoroutineContext
import kotlinx.coroutines.delay
import kotlinx.coroutines.coroutineScope
import kotlinx.coroutines.ensureActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.collectLatest
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.withContext
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import org.json.JSONObject

/**
 * Phone-hosted games (protocol/hosted-v1.md): this phone runs the game and
 * Atlas shows it. [host] claims the table, pushes every new state, renews the
 * lease while the game is quiet and releases it when cancelled. Atlas never
 * decides anything about the game.
 */
class HostedAtlasLink(
    /** A signed-in request to Atlas (the player session's raw call). */
    private val call: suspend (method: String, path: String, fields: List<Pair<String, String>>) -> RawResponse?,
    /** Under Atlas's 30 s lease, so a quiet game keeps the table. */
    private val renewMs: Long = 10_000,
    /** How often to read Atlas's queued inputs: a Sigil press should feel immediate. */
    private val inputPollMs: Long = 300,
) {
    sealed interface Status {
        data object Off : Status
        data object Hosting : Status
        /** Another phone holds this table. */
        data class Busy(val host: String?) : Status
        data class Following(val host:String?) : Status
        data class Failed(val message: String) : Status
    }

    private val _status = MutableStateFlow<Status>(Status.Off)
    private val hostMutex = Mutex()
    val status: StateFlow<Status> = _status.asStateFlow()
    fun followingRoom(host:String?) { if(_status.value!=Status.Hosting) _status.value=Status.Following(host) }

    /** One input from Atlas's own controls (a Sigil key, later the Atlas screen). */
    data class Input(val seq: Long, val id: String, val player: Int, val action: JSONObject)

    /**
     * Runs until cancelled. [games] is the core's game JSON, newest last; [apply] gets each
     * input Atlas's controls queued, once, for this phone's rules to accept or ignore.
     */
    suspend fun host(games: Flow<JSONObject>, apply: suspend (Input) -> Unit = {}) = hostMutex.withLock {
        try {
            while (!claim()) {
                if (_status.value is Status.Busy) return@withLock
                delay(renewMs)
            }
            coroutineScope {
                launch { pollInputs(apply) }
                games.distinctUntilChanged { a, b -> a.toString() == b.toString() }.collectLatest { game ->
                    val latest = game.toString()
                    // A push renews the lease; while nothing changes, re-send to keep it.
                    while (true) {
                        currentCoroutineContext().ensureActive()
                        if (!publish(latest) && _status.value is Status.Busy) return@collectLatest
                        delay(renewMs)
                    }
                }
            }
        } finally {
            withContext(NonCancellable) {
                if (_status.value == Status.Hosting) runCatching { call("POST", "/api/hosted/release", emptyList()) }
                if (_status.value !is Status.Busy && _status.value !is Status.Following) _status.value = Status.Off
            }
        }
    }

    /** An unfinished game the last hub left on Atlas, for this phone to resume. */
    data class Resume(val host: String, val game: JSONObject)

    /** The game to resume while no phone hosts the table, or null (none, or Atlas not reached). */
    suspend fun resumable(): Resume? {
        val response = runCatching { call("GET", "/api/hosted/state", emptyList()) }.getOrNull()
        if (response?.ok != true) return null
        return runCatching {
            val resume = JSONObject(response.body).optJSONObject("resume") ?: return null
            Resume(resume.optString("host"), resume.getJSONObject("game"))
        }.getOrNull()
    }

    private suspend fun pollInputs(apply: suspend (Input) -> Unit) {
        var after = 0L
        val seen = LinkedHashSet<String>()
        while (true) {
            currentCoroutineContext().ensureActive()
            if (_status.value == Status.Hosting) {
                val response = runCatching { call("GET", "/api/hosted/inputs", listOf("after" to "$after")) }.getOrNull()
                if (response?.ok == true) runCatching {
                    val body = JSONObject(response.body)
                    val list = body.getJSONArray("inputs")
                    for (i in 0 until list.length()) {
                        val item = list.getJSONObject(i)
                        val input = Input(item.getLong("seq"), item.getString("id"), item.getInt("player"), item.getJSONObject("action"))
                        after = maxOf(after, input.seq)
                        // Each action once, even if Atlas restarts its numbering.
                        if (seen.add(input.id)) apply(input)
                        while (seen.size > 512) seen.remove(seen.first())
                    }
                    // Atlas restarted (its numbering went back): read from the start.
                    if (body.optLong("last", after) < after) after = 0
                }
            }
            delay(inputPollMs)
        }
    }

    private suspend fun claim(): Boolean {
        val response = runCatching { call("POST", "/api/hosted/claim", emptyList()) }.getOrNull()
        _status.value = when {
            response == null -> Status.Failed("Sign in to Atlas to show this game on it")
            response.ok -> Status.Hosting
            response.code == 409 -> Status.Busy(hostOf(response.body))
            else -> Status.Failed(messageOf(response.body) ?: "Atlas could not show this game")
        }
        return _status.value == Status.Hosting
    }

    private suspend fun publish(state: String, retry: Boolean = true): Boolean {
        val response = runCatching { call("POST", "/api/hosted/state", listOf("state" to state)) }.getOrNull()
        if (response?.ok == true) { _status.value = Status.Hosting; return true }
        // Lost the lease (Atlas restarted, or the phone was away too long): take it back if it is free.
        if (response?.code == 409 && retry) return claim() && publish(state, retry = false)
        if (response?.code == 409) { _status.value = Status.Busy(null); return false }
        _status.value = Status.Failed(response?.let { messageOf(it.body) } ?: "Atlas stopped answering")
        return false
    }

    private fun hostOf(body: String): String? = runCatching { JSONObject(body).optString("host").takeIf { it.isNotBlank() && it != "null" } }.getOrNull()
    private fun messageOf(body: String): String? = runCatching { JSONObject(body).optString("error").takeIf { it.isNotBlank() } }.getOrNull()
}
