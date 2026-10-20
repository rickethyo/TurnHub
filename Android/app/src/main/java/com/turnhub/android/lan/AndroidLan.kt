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
import java.util.Calendar
import java.util.Date
import javax.net.ssl.KeyManagerFactory
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
    val alias = "turnhub_lan_host_v1"
    val store = KeyStore.getInstance("AndroidKeyStore").apply { load(null) }
    if (!store.containsAlias(alias)) {
        val end = Calendar.getInstance().apply { add(Calendar.YEAR, 20) }.time
        KeyPairGenerator.getInstance(KeyProperties.KEY_ALGORITHM_RSA, "AndroidKeyStore").apply {
            initialize(KeyGenParameterSpec.Builder(alias, KeyProperties.PURPOSE_SIGN or KeyProperties.PURPOSE_DECRYPT)
                .setKeySize(2048).setDigests(KeyProperties.DIGEST_SHA256, KeyProperties.DIGEST_SHA384, KeyProperties.DIGEST_SHA512)
                .setSignaturePaddings(KeyProperties.SIGNATURE_PADDING_RSA_PKCS1, KeyProperties.SIGNATURE_PADDING_RSA_PSS)
                .setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_RSA_PKCS1, KeyProperties.ENCRYPTION_PADDING_RSA_OAEP)
                .setCertificateSubject(X500Principal("CN=TurnHub shared game"))
                .setCertificateSerialNumber(BigInteger.ONE).setCertificateNotBefore(Date(0)).setCertificateNotAfter(end).build())
            generateKeyPair()
        }
    }
    val managers = KeyManagerFactory.getInstance(KeyManagerFactory.getDefaultAlgorithm()).apply { init(store, null) }
    val context = SSLContext.getInstance("TLS").apply { init(managers.keyManagers, null, null) }
    val pin = MessageDigest.getInstance("SHA-256").digest(store.getCertificate(alias).encoded).hex()
    return context to pin
}
