package com.turnhub.android.data

import java.net.URI
import java.net.URISyntaxException

/**
 * Where to reach an Atlas. All endpoints use HTTPS; a setup access point may
 * temporarily bootstrap its self-signed key, while home endpoints are pinned.
 *
 * This is chosen by the user (defaulting to [DEFAULT], Atlas's standalone
 * access-point address) and handed to [AtlasRepository.connect]; transports are
 * built for a given endpoint rather than owning one. [bootstrapTls] is only
 * valid for the known setup address and is restricted by [HttpAtlasTransport]
 * to public discovery and phone-pairing routes until the pin is confirmed.
 */
@ConsistentCopyVisibility
data class AtlasEndpoint private constructor(
    val host: String,
    val port: Int?,
    val tlsPin: String? = null,
    val bootstrapTls: Boolean = false,
) {

    /** Atlas always uses HTTPS, including on its setup access point. */
    val secure: Boolean get() = true

    /** The well-known setup access point, with the default HTTPS port. */
    val isSetupAccessPoint: Boolean get() = host == DEFAULT_HOST && port == null

    /** The HTTPS origin requests are resolved against. */
    val baseUrl: String = "https://" + host + (port?.let { ":$it" } ?: "")

    override fun toString(): String = baseUrl

    fun withTlsPin(pin: String): AtlasEndpoint {
        require(pin.matches(Regex("[0-9a-fA-F]{64}"))) { "TLS pin must be a SHA-256 hex digest" }
        return AtlasEndpoint(host, port, pin.lowercase())
    }

    companion object {
        private const val DEFAULT_HOST = "192.168.4.1"

        /** Atlas's standalone SoftAP address (Atlas/README.md). */
        val DEFAULT = AtlasEndpoint(DEFAULT_HOST, null, bootstrapTls = true)

        /** Atlas on the home network: HTTPS at [address], trusted only with [tlsPin]. */
        fun home(address: String, port: Int, tlsPin: String): AtlasEndpoint =
            AtlasEndpoint(address.lowercase(), port.takeIf { it != 443 }, tlsPin.lowercase())

        /**
         * Parses user input such as `192.168.4.1` or `https://atlas.local:8443`.
         * Plain HTTP is deliberately rejected.
         */
        fun parse(text: String): Result<AtlasEndpoint> {
            val trimmed = text.trim()
            if (trimmed.isEmpty()) return invalid("Enter the Atlas address")
            val withScheme = if ("://" in trimmed) trimmed else "https://$trimmed"
            val uri = try {
                URI(withScheme)
            } catch (_: URISyntaxException) {
                return invalid("'$trimmed' is not a valid address")
            }
            if (!uri.scheme.equals("https", ignoreCase = true)) {
                return invalid("Atlas connections require HTTPS; unencrypted HTTP is disabled.")
            }
            val host = uri.host ?: return invalid("'$trimmed' has no host")
            if (uri.rawUserInfo != null || uri.rawQuery != null || uri.rawFragment != null ||
                !(uri.rawPath.isNullOrEmpty() || uri.rawPath == "/")
            ) {
                return invalid("Enter only the Atlas address, without a path")
            }
            val normalizedHost = host.lowercase()
            val port = uri.port.takeIf { it != -1 && it != 443 }
            val bootstrap = normalizedHost == DEFAULT_HOST && port == null
            return Result.success(AtlasEndpoint(normalizedHost, port, bootstrapTls = bootstrap))
        }

        private fun invalid(detail: String): Result<AtlasEndpoint> =
            Result.failure(AtlasException(AtlasFailure.InvalidEndpoint(detail)))
    }
}
