package com.turnhub.android.data

import android.content.Context
import android.net.nsd.NsdManager
import android.net.nsd.NsdServiceInfo
import androidx.core.content.edit
import com.turnhub.android.protocol.AtlasHomeAddress
import com.turnhub.android.protocol.AtlasInfo
import kotlinx.coroutines.suspendCancellableCoroutine
import kotlinx.coroutines.withTimeoutOrNull
import kotlin.coroutines.resume

/** Where the last Atlas answered on the home network, and the key it must present. */
data class SavedHomeAtlas(val atlasId: String, val address: String, val port: Int, val pin: String) {
    val endpoint: AtlasEndpoint get() = AtlasEndpoint.home(address, port, pin)
    /** Compare on Atlas itself before accepting a first home-network connection. */
    val linkCode: String get() = pin.take(12).uppercase().chunked(4).joinToString("-")
}

interface HomeAtlasStore {
    fun last(): SavedHomeAtlas?
    fun save(home: SavedHomeAtlas)
}

class MemoryHomeAtlasStore : HomeAtlasStore {
    private var saved: SavedHomeAtlas? = null
    override fun last() = saved
    override fun save(home: SavedHomeAtlas) { saved = home }
}

/** In the same backup-excluded preferences file as the Atlas Wi-Fi passwords. */
class PreferencesHomeAtlasStore(context: Context) : HomeAtlasStore {
    private val prefs = context.applicationContext.getSharedPreferences(PreferencesWifiCredentialStore.FILE, Context.MODE_PRIVATE)

    override fun last(): SavedHomeAtlas? {
        val id = prefs.getString("home_atlas", null) ?: return null
        val address = prefs.getString("home_address", null) ?: return null
        val pin = prefs.getString("home_pin", null) ?: return null
        return SavedHomeAtlas(id, address, prefs.getInt("home_port", 443), pin)
    }

    override fun save(home: SavedHomeAtlas) = prefs.edit {
        putString("home_atlas", home.atlasId)
        putString("home_address", home.address)
        putInt("home_port", home.port)
        putString("home_pin", home.pin)
    }
}

/**
 * Atlas on the home network, Atlas's primary path (Documentation/engineering/HOME_WIFI.md).
 *
 * The app learns where Atlas is and which key it holds only over Atlas's own
 * hotspot ([learn]), the network it already trusts. Later it tries that
 * pinned HTTPS address first ([find]), then looks for the same key on the
 * home network by mDNS, and only then falls back to joining the hotspot.
 */
class AtlasHomeLink(
    private val store: HomeAtlasStore,
    /** `/api/v1/info` at an endpoint (pinned when HTTPS); throws when it doesn't answer. */
    private val info: suspend (AtlasEndpoint) -> AtlasInfo,
    /** Atlas's current home address advertising [pin], or null (mDNS). */
    private val discover: suspend (pin: String) -> Pair<String, Int>? = { null },
    private val discoverNew: suspend () -> SavedHomeAtlas? = { null },
) {
    fun hasSavedAtlas(): Boolean = store.last() != null
    fun hasSavedAtlas(atlasId: String?): Boolean = atlasId != null &&
        store.last()?.atlasId?.equals(atlasId, ignoreCase = true) == true

    /** Discovery is not trust: nothing is saved until the user compares the physical code. */
    suspend fun pairingCandidate(): SavedHomeAtlas? {
        if (hasSavedAtlas()) return null
        val candidate = discoverNew() ?: return null
        return candidate.takeIf { answers(it) }
    }

    /** Never replace an existing trusted key with an advertisement from the LAN. */
    suspend fun confirmPairing(candidate: SavedHomeAtlas): AtlasEndpoint? {
        if (hasSavedAtlas() || !answers(candidate)) return null
        store.save(candidate)
        return candidate.endpoint
    }

    /** Over the hotspot only: remember Atlas's home address and key. */
    suspend fun learn(endpoint: AtlasEndpoint): Boolean {
        if (endpoint != AtlasEndpoint.DEFAULT) return false
        val current = runCatching { info(endpoint) }.getOrNull() ?: return false
        val home = current.home ?: return false
        store.save(saved(current.atlasId, home))
        return true
    }

    /** A pinned home endpoint that answers as the saved Atlas, or null. */
    suspend fun find(): AtlasEndpoint? {
        val saved = store.last() ?: return null
        if (answers(saved)) return saved.endpoint
        val (address, port) = discover(saved.pin) ?: return null
        val moved = saved.copy(address = address, port = port)
        if (!answers(moved)) return null
        store.save(moved)
        return moved.endpoint
    }

    // The pin is checked in the handshake; the ID makes sure it is the same table.
    private suspend fun answers(saved: SavedHomeAtlas): Boolean =
        runCatching { info(saved.endpoint).atlasId.equals(saved.atlasId, ignoreCase = true) }.getOrDefault(false)

    private fun saved(atlasId: String, home: AtlasHomeAddress) =
        SavedHomeAtlas(atlasId, home.address, home.tlsPort, home.tlsPin)
}

/** Finds `_turnhub-atlas._tcp` with a matching `fp` on the phone's network. */
class NsdAtlasDiscovery(context: Context, private val timeoutMs: Long = 4_000) {
    private val nsd: NsdManager? = context.applicationContext.getSystemService(NsdManager::class.java)

    suspend fun find(pin: String): Pair<String, Int>? = resolve(pin)?.let { it.address to it.port }

    suspend fun findNew(): SavedHomeAtlas? = resolve(null)

    private suspend fun resolve(pin: String?): SavedHomeAtlas? {
        val manager = nsd ?: return null
        return withTimeoutOrNull(timeoutMs) {
            suspendCancellableCoroutine { continuation ->
                lateinit var listener: NsdManager.DiscoveryListener
                fun stop() = runCatching { manager.stopServiceDiscovery(listener) }
                listener = object : NsdManager.DiscoveryListener {
                    override fun onDiscoveryStarted(serviceType: String) = Unit
                    override fun onDiscoveryStopped(serviceType: String) = Unit
                    override fun onStartDiscoveryFailed(serviceType: String, errorCode: Int) {
                        if (continuation.isActive) continuation.resume(null)
                    }
                    override fun onStopDiscoveryFailed(serviceType: String, errorCode: Int) = Unit
                    override fun onServiceLost(service: NsdServiceInfo) = Unit
                    override fun onServiceFound(service: NsdServiceInfo) {
                        @Suppress("DEPRECATION")
                        manager.resolveService(service, object : NsdManager.ResolveListener {
                            override fun onResolveFailed(info: NsdServiceInfo, errorCode: Int) = Unit
                            override fun onServiceResolved(info: NsdServiceInfo) {
                                val fp = info.attributes["fp"]?.toString(Charsets.UTF_8)
                                val id = info.attributes["id"]?.toString(Charsets.UTF_8)
                                val tls = info.attributes["tls"]?.toString(Charsets.UTF_8)
                                @Suppress("DEPRECATION")
                                val host = info.host?.hostAddress
                                if (fp != null && fp.matches(Regex("[0-9a-fA-F]{64}")) &&
                                    (pin == null || fp.equals(pin, ignoreCase = true)) && tls == "1" &&
                                    id != null && id.matches(Regex("THA-[0-9a-fA-F]{12}")) &&
                                    host != null && info.port in 1..65535 && continuation.isActive) {
                                    stop()
                                    continuation.resume(SavedHomeAtlas(id.uppercase(), host, info.port, fp.lowercase()))
                                }
                            }
                        })
                    }
                }
                continuation.invokeOnCancellation { stop() }
                runCatching { manager.discoverServices(SERVICE_TYPE, NsdManager.PROTOCOL_DNS_SD, listener) }
                    .onFailure { if (continuation.isActive) continuation.resume(null) }
            }
        }
    }

    private companion object {
        const val SERVICE_TYPE = "_turnhub-atlas._tcp."
    }
}
