package com.turnhub.android.data

import java.net.URI
import java.net.URISyntaxException

/**
 * Where to reach an Atlas: an `http://host[:port]` origin with no path.
 *
 * This is chosen by the user (defaulting to [DEFAULT], Atlas's standalone
 * access-point address) and handed to [AtlasRepository.connect]; transports are
 * built for a given endpoint rather than owning one. Atlas serves plain HTTP on
 * its local network, so only `http` is accepted.
 */
@ConsistentCopyVisibility
data class AtlasEndpoint private constructor(val host: String, val port: Int?) {

    /** The origin requests are resolved against, e.g. `http://192.168.4.1`. */
    val baseUrl: String = if (port == null) "http://$host" else "http://$host:$port"

    override fun toString(): String = baseUrl

    companion object {
        /** Atlas's standalone SoftAP address (Atlas/README.md). */
        val DEFAULT = AtlasEndpoint("192.168.4.1", null)

        /**
         * Parses user input such as `192.168.4.1`, `http://192.168.4.1/` or
         * `http://atlas.local:8080`. Returns [AtlasFailure.InvalidEndpoint] as the
         * failure on anything that isn't a bare HTTP origin.
         */
        fun parse(text: String): Result<AtlasEndpoint> {
            val trimmed = text.trim()
            if (trimmed.isEmpty()) return invalid("Enter the Atlas address")
            val withScheme = if ("://" in trimmed) trimmed else "http://$trimmed"
            val uri = try {
                URI(withScheme)
            } catch (_: URISyntaxException) {
                return invalid("'$trimmed' is not a valid address")
            }
            if (!uri.scheme.equals("http", ignoreCase = true)) {
                return invalid("Atlas uses plain http:// on its local network")
            }
            val host = uri.host ?: return invalid("'$trimmed' has no host")
            if (uri.rawUserInfo != null || uri.rawQuery != null || uri.rawFragment != null ||
                !(uri.rawPath.isNullOrEmpty() || uri.rawPath == "/")
            ) {
                return invalid("Enter only the Atlas address, without a path")
            }
            return Result.success(AtlasEndpoint(host.lowercase(), uri.port.takeIf { it != -1 }))
        }

        private fun invalid(detail: String): Result<AtlasEndpoint> =
            Result.failure(AtlasException(AtlasFailure.InvalidEndpoint(detail)))
    }
}
