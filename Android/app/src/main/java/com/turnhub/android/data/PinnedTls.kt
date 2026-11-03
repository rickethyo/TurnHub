package com.turnhub.android.data

import java.security.MessageDigest
import java.security.PublicKey
import java.security.SecureRandom
import java.security.cert.CertificateException
import java.security.cert.X509Certificate
import javax.net.ssl.HostnameVerifier
import javax.net.ssl.HttpsURLConnection
import javax.net.ssl.SSLContext
import javax.net.ssl.SSLSocketFactory
import javax.net.ssl.X509TrustManager

/**
 * HTTPS to Atlas on the home network (Documentation/engineering/HOME_WIFI.md).
 *
 * Atlas has a self-signed key and no CA, and its address changes, so the app
 * trusts exactly one key: the SHA-256 of its SubjectPublicKeyInfo (the pin)
 * learned over Atlas's own hotspot. Any other key fails the handshake, and
 * the pin stands in for the hostname check.
 */
object PinnedTls {

    /** Lower-case hex SHA-256 of [key]'s SubjectPublicKeyInfo, as Atlas reports it. */
    fun pinOf(key: PublicKey): String =
        MessageDigest.getInstance("SHA-256").digest(key.encoded).joinToString("") { "%02x".format(it) }

    /** Trusts a server whose leaf key matches [pin]; never a client. */
    class TrustManager(private val pin: String) : X509TrustManager {
        override fun checkClientTrusted(chain: Array<out X509Certificate>?, authType: String?) =
            throw CertificateException("Atlas never asks for a client certificate")

        override fun checkServerTrusted(chain: Array<out X509Certificate>?, authType: String?) {
            val leaf = chain?.firstOrNull() ?: throw CertificateException("Atlas sent no certificate")
            if (pinOf(leaf.publicKey) != pin) throw CertificateException("This isn't the Atlas this phone knows")
        }

        override fun getAcceptedIssuers(): Array<X509Certificate> = emptyArray()
    }

    private val factories = HashMap<String, SSLSocketFactory>()

    /** One factory per pin, so TLS sessions resume across requests. */
    @Synchronized
    fun socketFactory(pin: String): SSLSocketFactory = factories.getOrPut(pin) {
        SSLContext.getInstance("TLS").apply { init(null, arrayOf(TrustManager(pin)), SecureRandom()) }.socketFactory
    }

    /**
     * Atlas's address changes, so the hostname is checked by the key instead:
     * the session's server key must be the pinned one (as in [TrustManager]).
     */
    private fun pinnedHost(pin: String) = HostnameVerifier { _, session ->
        runCatching { pinOf(session.peerCertificates.first().publicKey) == pin }.getOrDefault(false)
    }

    /** Opens through [base] and, for HTTPS, trusts only [pin]. */
    fun opener(base: HttpConnectionOpener, pin: String): HttpConnectionOpener = HttpConnectionOpener { url ->
        base.open(url).also { connection ->
            if (connection is HttpsURLConnection) {
                connection.sslSocketFactory = socketFactory(pin)
                connection.hostnameVerifier = pinnedHost(pin)
            }
        }
    }
}
