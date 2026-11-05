package com.turnhub.android.data

import android.content.Context
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import org.json.JSONObject

/** This phone's device key for each Atlas it paired with, by Atlas ID. Keep it private. */
interface AtlasPhoneKeyStore {
    fun key(atlasId: String): String?
    fun save(atlasId: String, credential: String)
    fun forget(atlasId: String)
}

class PreferencesAtlasPhoneKeyStore(context: Context) : AtlasPhoneKeyStore {
    private val prefs = context.getSharedPreferences("turnhub_atlas_phones", Context.MODE_PRIVATE)
    override fun key(atlasId: String): String? = prefs.getString(atlasId, null)
    override fun save(atlasId: String, credential: String) { prefs.edit().putString(atlasId, credential).apply() }
    override fun forget(atlasId: String) { prefs.edit().remove(atlasId).apply() }
}

class MemoryAtlasPhoneKeyStore : AtlasPhoneKeyStore {
    private val keys = mutableMapOf<String, String>()
    override fun key(atlasId: String): String? = keys[atlasId]
    override fun save(atlasId: String, credential: String) { keys[atlasId] = credential }
    override fun forget(atlasId: String) { keys.remove(atlasId) }
}

/**
 * Pairing replaces signing in to Atlas (owner, 2026-11-05; protocol/hosted-v1.md):
 * the phone asks once, Atlas shows "Allow this phone?" with the same code,
 * and the device key it hands back goes in `X-TurnHub-Token` for hosting.
 */
class AtlasPhonePairing(
    private val keys: AtlasPhoneKeyStore,
    private val transports: AtlasSessionTransportFactory,
    private val pollMs: Long = 1_000,
) {
    sealed interface State {
        data object Idle : State
        /** Waiting for Allow on the Atlas screen; [code] is shown on both. */
        data class Asking(val code: String) : State
        data object Paired : State
        data class Failed(val message: String) : State
    }

    private val _state = MutableStateFlow<State>(State.Idle)
    val state: StateFlow<State> = _state.asStateFlow()

    fun paired(atlasId: String?): Boolean = atlasId != null && keys.key(atlasId) != null

    /** Asks Atlas to pair and waits for the Atlas screen's answer; true once the key is saved. */
    suspend fun pair(endpoint: AtlasEndpoint, atlasId: String, phoneName: String): Boolean {
        if (paired(atlasId)) { _state.value = State.Paired; return true }
        val transport = transports.create(endpoint)
        try {
            val asked = transport.raw("POST", "/api/phones/pair", "", listOf("name" to phoneName.take(32)))
            if (!asked.ok) {
                _state.value = State.Failed(message(asked, "Atlas couldn't start pairing. Try again."))
                return false
            }
            val ask = JSONObject(asked.body)
            val request = ask.getString("request")
            _state.value = State.Asking(ask.getString("code"))
            val until = System.currentTimeMillis() + ask.optLong("waitMs", 60_000) + 5_000
            while (System.currentTimeMillis() < until) {
                delay(pollMs)
                val polled = runCatching { transport.raw("GET", "/api/phones/pair", "", listOf("request" to request)) }
                    .getOrElse { if (it is CancellationException) throw it; null } ?: continue
                if (!polled.ok) continue
                val answer = JSONObject(polled.body)
                when (answer.optString("state")) {
                    "waiting" -> Unit
                    "allowed" -> {
                        keys.save(atlasId, "phone:${answer.getString("phone")}:${answer.getString("key")}")
                        _state.value = State.Paired
                        return true
                    }
                    "refused" -> { _state.value = State.Failed("Atlas refused this phone."); return false }
                    else -> { _state.value = State.Failed("Pairing timed out. Try again."); return false }
                }
            }
            _state.value = State.Failed("Pairing timed out. Try again.")
            return false
        } catch (e: CancellationException) {
            if (_state.value is State.Asking) _state.value = State.Idle
            throw e
        } catch (_: Exception) {
            _state.value = State.Failed("Couldn't reach Atlas to pair. Try again.")
            return false
        }
    }

    /** A request with this phone's key; null without one. A 401 means Atlas forgot this phone. */
    suspend fun raw(endpoint: AtlasEndpoint, atlasId: String, method: String, path: String,
                    fields: List<Pair<String, String>>): RawResponse? {
        val key = keys.key(atlasId) ?: return null
        return forgetOn401(atlasId, transports.create(endpoint).raw(method, path, key, fields))
    }

    suspend fun rawText(endpoint: AtlasEndpoint, atlasId: String, path: String,
                        fields: List<Pair<String, String>>, body: String): RawResponse? {
        val key = keys.key(atlasId) ?: return null
        return forgetOn401(atlasId, transports.create(endpoint).rawText(path, key, fields, body))
    }

    fun clearMessage() { if (_state.value is State.Failed) _state.value = State.Idle }

    private fun forgetOn401(atlasId: String, response: RawResponse): RawResponse {
        if (response.code == 401) {
            keys.forget(atlasId)
            _state.value = State.Idle
        }
        return response
    }

    private fun message(response: RawResponse, fallback: String): String =
        runCatching { JSONObject(response.body).optString("error") }.getOrNull()?.takeIf { it.isNotBlank() } ?: fallback
}
