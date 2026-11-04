package com.turnhub.android.data

import kotlinx.coroutines.NonCancellable
import kotlinx.coroutines.currentCoroutineContext
import kotlinx.coroutines.delay
import kotlinx.coroutines.ensureActive
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.collectLatest
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.withContext
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
) {
    sealed interface Status {
        data object Off : Status
        data object Hosting : Status
        /** Another phone holds this table. */
        data class Busy(val host: String?) : Status
        data class Failed(val message: String) : Status
    }

    private val _status = MutableStateFlow<Status>(Status.Off)
    val status: StateFlow<Status> = _status.asStateFlow()

    /** Runs until cancelled. [games] is the core's game JSON, newest last. */
    suspend fun host(games: Flow<JSONObject>) {
        try {
            if (!claim()) return
            var latest: String? = null
            games.distinctUntilChanged { a, b -> a.toString() == b.toString() }.collectLatest { game ->
                latest = game.toString()
                // A push renews the lease; while nothing changes, re-send to keep it.
                while (true) {
                    currentCoroutineContext().ensureActive()
                    if (!publish(latest!!)) return@collectLatest
                    delay(renewMs)
                }
            }
        } finally {
            withContext(NonCancellable) {
                if (_status.value == Status.Hosting) runCatching { call("POST", "/api/hosted/release", emptyList()) }
                if (_status.value !is Status.Busy) _status.value = Status.Off
            }
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
