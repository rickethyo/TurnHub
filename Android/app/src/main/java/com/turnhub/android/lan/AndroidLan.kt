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
