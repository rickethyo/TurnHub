package com.turnhub.android.lan

import android.content.Context
import android.net.ConnectivityManager
import android.net.Network
import android.net.NetworkCapabilities
import android.security.keystore.KeyGenParameterSpec
import android.security.keystore.KeyProperties
import java.math.BigInteger
import java.net.Inet4Address
import java.net.InetAddress
import java.security.KeyPairGenerator
import java.security.KeyStore
import java.security.MessageDigest
import java.security.PrivateKey
import java.security.cert.X509Certificate
import java.security.spec.ECGenParameterSpec
import java.util.Calendar
import java.util.Date
import javax.net.ssl.SSLContext
import javax.security.auth.x500.X500Principal

internal data class LanWifi(val network: Network, val address: InetAddress)

/** Use the current Wi-Fi explicitly even when cellular/VPN is the default route. */
internal fun currentLanWifi(context: Context): LanWifi {
    val manager = context.getSystemService(ConnectivityManager::class.java)
    @Suppress("DEPRECATION")
    val networks = manager.allNetworks
    return (listOfNotNull(manager.activeNetwork) + networks).distinct().firstNotNullOfOrNull { network ->
        val capabilities = manager.getNetworkCapabilities(network) ?: return@firstNotNullOfOrNull null
        if (!capabilities.hasTransport(NetworkCapabilities.TRANSPORT_WIFI) || capabilities.hasTransport(NetworkCapabilities.TRANSPORT_VPN))
            return@firstNotNullOfOrNull null
        val address = manager.getLinkProperties(network)?.linkAddresses?.map { it.address }
            ?.firstOrNull { it is Inet4Address && privateIpv4(it.hostAddress.orEmpty()) } ?: return@firstNotNullOfOrNull null
        LanWifi(network, address)
    } ?: error("Join the same Wi-Fi network on every device, then try again.")
}

internal fun lanServerContext(): Pair<SSLContext, String> {
    // Conscrypt's RSA TLS bridge requests raw private-key operations that the
    // restricted v1 RSA key cannot perform on some Android keystores. ECDSA
    // uses the signing API with a digest already computed by TLS, so authorize
    // DIGEST_NONE as well as SHA-256 for certificate generation.
    val alias = "turnhub_lan_host_ec_v3"
    val store = KeyStore.getInstance("AndroidKeyStore").apply { load(null) }
    if (!store.containsAlias(alias)) {
        val end = Calendar.getInstance().apply { add(Calendar.YEAR, 20) }.time
        KeyPairGenerator.getInstance(KeyProperties.KEY_ALGORITHM_EC, "AndroidKeyStore").apply {
            initialize(KeyGenParameterSpec.Builder(alias, KeyProperties.PURPOSE_SIGN)
                .setAlgorithmParameterSpec(ECGenParameterSpec("secp256r1"))
                .setDigests(KeyProperties.DIGEST_NONE, KeyProperties.DIGEST_SHA256)
                .setCertificateSubject(X500Principal("CN=TurnHub shared game"))
                .setCertificateSerialNumber(BigInteger.ONE).setCertificateNotBefore(Date(0)).setCertificateNotAfter(end).build())
            generateKeyPair()
        }
    }
    val manager = lanHostKeyManager(store.getKey(alias, null) as PrivateKey,
        store.getCertificateChain(alias).map { it as X509Certificate }.toTypedArray())
    val context = SSLContext.getInstance("TLS").apply { init(arrayOf(manager), null, null) }
    val pin = MessageDigest.getInstance("SHA-256").digest(store.getCertificate(alias).encoded).hex()
    return context to pin
}

internal const val LAN_SERVICE_TYPE = "_turnhub._tcp"

/** A host found on the Wi-Fi. Its TXT record is unauthenticated: only the code or QR key grants access. */
internal data class LanHost(val address: String, val port: Int, val pin: String, val session: String)

/**
 * Announces a standard-joining host on the Wi-Fi so guests can find it with only its code.
 * The record names the session and certificate, never the code. QR-only hosts don't announce.
 */
internal class LanAdvertiser(context: Context, network: Network, port: Int, session: String, pin: String) : AutoCloseable {
    private val nsd = context.getSystemService(android.net.nsd.NsdManager::class.java)
    private val listener = object : android.net.nsd.NsdManager.RegistrationListener {
        override fun onServiceRegistered(info: android.net.nsd.NsdServiceInfo) = Unit
        override fun onRegistrationFailed(info: android.net.nsd.NsdServiceInfo, error: Int) = Unit
        override fun onServiceUnregistered(info: android.net.nsd.NsdServiceInfo) = Unit
        override fun onUnregistrationFailed(info: android.net.nsd.NsdServiceInfo, error: Int) = Unit
    }
    init {
        val info = android.net.nsd.NsdServiceInfo().apply {
            serviceName = "TurnHub shared game"
            serviceType = LAN_SERVICE_TYPE
            this.port = port
            setAttribute("v", "$LAN_VERSION")
            setAttribute("s", session)
            setAttribute("p", pin)
            if (android.os.Build.VERSION.SDK_INT >= 33) this.network = network
        }
        nsd.registerService(info, android.net.nsd.NsdManager.PROTOCOL_DNS_SD, listener)
    }
    override fun close() { runCatching { nsd.unregisterService(listener) } }
}

/** Every TurnHub host announcing on [wifi] within [windowMs]. Resolves one at a time, as older Android requires. */
internal suspend fun discoverLanHosts(context: Context, wifi: LanWifi, windowMs: Long = 3_000): List<LanHost> {
    val nsd = context.getSystemService(android.net.nsd.NsdManager::class.java)
    val found = kotlinx.coroutines.channels.Channel<android.net.nsd.NsdServiceInfo>(kotlinx.coroutines.channels.Channel.UNLIMITED)
    val listener = object : android.net.nsd.NsdManager.DiscoveryListener {
        override fun onDiscoveryStarted(type: String) = Unit
        override fun onDiscoveryStopped(type: String) = Unit
        override fun onStartDiscoveryFailed(type: String, error: Int) { found.close() }
        override fun onStopDiscoveryFailed(type: String, error: Int) = Unit
        override fun onServiceFound(info: android.net.nsd.NsdServiceInfo) { found.trySend(info) }
        override fun onServiceLost(info: android.net.nsd.NsdServiceInfo) = Unit
    }
    if (android.os.Build.VERSION.SDK_INT >= 33)
        nsd.discoverServices(LAN_SERVICE_TYPE, android.net.nsd.NsdManager.PROTOCOL_DNS_SD, wifi.network, java.util.concurrent.Executors.newSingleThreadExecutor(), listener)
    else nsd.discoverServices(LAN_SERVICE_TYPE, android.net.nsd.NsdManager.PROTOCOL_DNS_SD, listener)
    val hosts = mutableListOf<LanHost>()
    try {
        val deadline = System.currentTimeMillis() + windowMs
        while (true) {
            val left = deadline - System.currentTimeMillis()
            if (left <= 0) break
            val info = kotlinx.coroutines.withTimeoutOrNull(left) { found.receiveCatching().getOrNull() } ?: break
            resolveLanHost(nsd, info)?.takeIf { it !in hosts }?.let(hosts::add)
        }
    } finally { runCatching { nsd.stopServiceDiscovery(listener) } }
    return hosts
}

@Suppress("DEPRECATION")
private suspend fun resolveLanHost(nsd: android.net.nsd.NsdManager, info: android.net.nsd.NsdServiceInfo): LanHost? =
    kotlinx.coroutines.withTimeoutOrNull(2_000) {
        kotlinx.coroutines.suspendCancellableCoroutine { done ->
            nsd.resolveService(info, object : android.net.nsd.NsdManager.ResolveListener {
                override fun onResolveFailed(info: android.net.nsd.NsdServiceInfo, error: Int) { if (done.isActive) done.resume(null) {} }
                override fun onServiceResolved(info: android.net.nsd.NsdServiceInfo) {
                    val text = { key: String -> info.attributes[key]?.toString(Charsets.UTF_8) }
                    val address = info.host?.hostAddress
                    val host = if (address != null && privateIpv4(address) && text("v") == "$LAN_VERSION" && info.port in 1024..65535 &&
                        text("s")?.matches(Regex("[0-9a-f]{32}")) == true && text("p")?.matches(Regex("[0-9a-f]{64}")) == true)
                        LanHost(address, info.port, text("p")!!, text("s")!!) else null
                    if (done.isActive) done.resume(host) {}
                }
            })
        }
    }
