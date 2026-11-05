package com.turnhub.android.data

import android.content.Context
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import org.json.JSONObject

/** This phone's device key for each Atlas it paired with, by Atlas ID. Keep it private. */
interface AtlasPhoneKeyStore {
    fun key(atlasId: String): String?
    fun tlsPin(atlasId: String): String? = null
    fun save(atlasId: String, credential: String)
    fun save(atlasId: String, credential: String, tlsPin: String?) {
        save(atlasId, credential)
    }
    fun forget(atlasId: String)
}

class PreferencesAtlasPhoneKeyStore(context: Context) : AtlasPhoneKeyStore {
    private val prefs = context.getSharedPreferences("turnhub_atlas_phones", Context.MODE_PRIVATE)
    override fun key(atlasId: String): String? = prefs.getString(atlasId, null)
    override fun tlsPin(atlasId: String): String? = prefs.getString(pinKey(atlasId), null)
    override fun save(atlasId: String, credential: String) = save(atlasId, credential, null)
    override fun save(atlasId: String, credential: String, tlsPin: String?) {
        val edit = prefs.edit().putString(atlasId, credential)
        if (tlsPin == null) edit.remove(pinKey(atlasId)) else edit.putString(pinKey(atlasId), tlsPin)
        if (!edit.commit()) {
            // A failed commit still changes SharedPreferences' in-memory map.
            prefs.edit().remove(atlasId).remove(pinKey(atlasId)).apply()
            error("Could not save the Atlas pairing key")
        }
    }

    override fun forget(atlasId: String) {
        prefs.edit().remove(atlasId).remove(pinKey(atlasId)).apply()
    }

    private fun pinKey(atlasId: String) = "tls:$atlasId"
}

class MemoryAtlasPhoneKeyStore : AtlasPhoneKeyStore {
    private val keys = mutableMapOf<String, String>()
    private val pins = mutableMapOf<String, String>()
    override fun key(atlasId: String): String? = keys[atlasId]
    override fun save(atlasId: String, credential: String) { keys[atlasId] = credential }
    override fun tlsPin(atlasId: String): String? = pins[atlasId]
    override fun save(atlasId: String, credential: String, tlsPin: String?) {
        keys[atlasId] = credential
        if (tlsPin == null) pins.remove(atlasId) else pins[atlasId] = tlsPin
    }
    override fun forget(atlasId: String) {
        keys.remove(atlasId)
        pins.remove(atlasId)
    }
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
    data class TrustedSetupEndpoint(val endpoint: AtlasEndpoint, val atlasId: String)

    sealed interface State {
        data object Idle : State
        /** Waiting for physical approval; first trust also requires phone-side fingerprint confirmation. */
        data class Asking(
            val code: String,
            val requiresFingerprintConfirmation: Boolean = false,
            val fingerprintConfirmed: Boolean = false,
        ) : State
        data object Paired : State
        data class Failed(val message: String) : State
    }

    private val _state = MutableStateFlow<State>(State.Idle)
    val state: StateFlow<State> = _state.asStateFlow()
    private var fingerprintConfirmation: CompletableDeferred<Boolean>? = null

    fun paired(atlasId: String?): Boolean = atlasId != null && keys.key(atlasId) != null

    /**
     * Discovers the setup Atlas over a restricted provisional TLS connection,
     * then either verifies its saved pin or requires the user to compare the
     * observed pin with Atlas before saving a new one.
     */
    suspend fun trustSetupEndpoint(endpoint: AtlasEndpoint, phoneName: String): TrustedSetupEndpoint? {
        require(endpoint.bootstrapTls) { "Only the setup endpoint may bootstrap TLS trust" }
        return try {
            val discovery = transports.create(endpoint)
            val info = setupInfoWithRetry(discovery)
            AtlasCompatibility.requireCompatible(info)
            val observedPin = discovery.tlsPeerPin
                ?.takeIf { it.matches(Regex("[0-9a-fA-F]{64}")) }
                ?: run {
                    _state.value = State.Failed("Atlas did not present a verifiable TLS identity.")
                    return null
                }
            val savedPin = keys.tlsPin(info.atlasId)
            if (savedPin != null) {
                if (!isTlsPin(savedPin)) {
                    _state.value = State.Failed("The saved Atlas TLS fingerprint is invalid. Forget and pair Atlas again.")
                    return null
                }
                if (!savedPin.equals(observedPin, ignoreCase = true)) {
                    _state.value = State.Failed("Atlas's TLS fingerprint changed. Verify the Atlas before trusting it again.")
                    return null
                }
                return TrustedSetupEndpoint(endpoint.withTlsPin(savedPin), info.atlasId)
            }

            val needsApproval = keys.key(info.atlasId) != null
            if (!pair(endpoint, info.atlasId, phoneName, requireApproval = needsApproval, expectedPin = observedPin)) {
                return null
            }
            val approvedPin = keys.tlsPin(info.atlasId)
            if (approvedPin == null || !approvedPin.equals(observedPin, ignoreCase = true)) {
                _state.value = State.Failed("Atlas's TLS fingerprint could not be saved. Try pairing again.")
                return null
            }
            TrustedSetupEndpoint(endpoint.withTlsPin(approvedPin), info.atlasId)
        } catch (e: CancellationException) {
            throw e
        } catch (e: AtlasException) {
            _state.value = State.Failed(e.failure.userMessage)
            null
        }
    }

    /** Asks Atlas to pair and waits for the Atlas screen's answer; true once the key is saved. */
    suspend fun pair(
        endpoint: AtlasEndpoint,
        atlasId: String,
        phoneName: String,
        requireApproval: Boolean = false,
        expectedPin: String? = null,
    ): Boolean {
        val endpointPin = endpoint.tlsPin
        if (!requireApproval && paired(atlasId) &&
            (endpointPin == null || keys.tlsPin(atlasId)?.equals(endpointPin, ignoreCase = true) == true)
        ) {
            _state.value = State.Paired
            return true
        }
        val transport = transports.create(endpoint)
        var requestId: String? = null
        try {
            val asked = transport.raw("POST", "/api/phones/pair", "", listOf("name" to phoneName.take(32)) +
                if (endpoint.secure) listOf("home" to "1") else emptyList())
            if (!asked.ok) {
                _state.value = State.Failed(message(asked, "Atlas couldn't start pairing. Try again."))
                return false
            }
            val ask = JSONObject(asked.body)
            val request = ask.getString("request")
            requestId = request
            val peerPin = transport.tlsPeerPin
            if ((expectedPin != null && peerPin != null && !peerPin.equals(expectedPin, ignoreCase = true)) ||
                (endpointPin != null && peerPin != null && !peerPin.equals(endpointPin, ignoreCase = true))
            ) {
                cancelRequest(transport, request)
                _state.value = State.Failed("Atlas's TLS fingerprint changed while pairing. Try again.")
                return false
            }
            val actualPin = peerPin ?: endpointPin
            if (endpoint.bootstrapTls && (actualPin == null || !isTlsPin(actualPin))) {
                cancelRequest(transport, request)
                _state.value = State.Failed("Atlas did not present a verifiable TLS identity.")
                return false
            }
            val needsFingerprintConfirmation = actualPin != null &&
                keys.tlsPin(atlasId)?.equals(actualPin, ignoreCase = true) != true
            val code = actualPin?.take(12)?.uppercase()?.chunked(4)?.joinToString(" ")
                ?: ask.getString("code")
            fingerprintConfirmation = if (needsFingerprintConfirmation) CompletableDeferred() else null
            _state.value = State.Asking(code, needsFingerprintConfirmation)
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
                        if (needsFingerprintConfirmation) {
                            val confirmed = kotlinx.coroutines.withTimeoutOrNull(FINGERPRINT_CONFIRM_TIMEOUT_MS) {
                                fingerprintConfirmation?.await()
                            } == true
                            if (!confirmed) {
                                _state.value = State.Failed("Confirm the matching TLS fingerprint on this phone before pairing.")
                                return false
                            }
                        }
                        keys.save(
                            atlasId,
                            "phone:${answer.getString("phone")}:${answer.getString("key")}",
                            actualPin,
                        )
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
            requestId?.let { cancelRequest(transport, it) }
            fingerprintConfirmation = null
            if (_state.value is State.Asking) _state.value = State.Idle
            throw e
        } catch (_: Exception) {
            _state.value = State.Failed("Couldn't reach Atlas to pair. Try again.")
            return false
        } finally {
            fingerprintConfirmation = null
        }
    }

    fun confirmFingerprint() {
        val asking = _state.value as? State.Asking ?: return
        if (!asking.requiresFingerprintConfirmation || asking.fingerprintConfirmed) return
        if (fingerprintConfirmation?.complete(true) == true) {
            _state.value = asking.copy(fingerprintConfirmed = true)
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

    private suspend fun cancelRequest(transport: AtlasSessionTransport, request: String) {
        kotlinx.coroutines.withContext(kotlinx.coroutines.NonCancellable) {
            kotlinx.coroutines.withTimeoutOrNull(3_000) {
                runCatching {
                    transport.raw("POST", "/api/phones/pair/cancel", "", listOf("request" to request))
                }
            }
        }
    }

    private suspend fun setupInfoWithRetry(transport: AtlasSessionTransport): com.turnhub.android.protocol.AtlasInfo {
        val deadline = System.nanoTime() + SETUP_TLS_READY_TIMEOUT_MS * 1_000_000
        var lastFailure: AtlasException? = null
        while (true) {
            try {
                return transport.getInfo()
            } catch (e: AtlasException) {
                if (e.failure !is AtlasFailure.Unreachable && e.failure !is AtlasFailure.Timeout) throw e
                lastFailure = e
            }
            val remainingMs = (deadline - System.nanoTime()) / 1_000_000
            if (remainingMs <= 0) throw lastFailure
            delay(minOf(SETUP_TLS_RETRY_MS, remainingMs))
        }
    }

    private companion object {
        const val FINGERPRINT_CONFIRM_TIMEOUT_MS = 60_000L
        const val SETUP_TLS_READY_TIMEOUT_MS = 12_000L
        const val SETUP_TLS_RETRY_MS = 300L

        fun isTlsPin(pin: String): Boolean = pin.matches(Regex("[0-9a-fA-F]{64}"))
    }
}
