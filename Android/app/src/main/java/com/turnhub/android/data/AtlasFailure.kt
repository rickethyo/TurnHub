package com.turnhub.android.data

/**
 * Why talking to an Atlas failed, phrased for the person holding the phone.
 * Transports and the repository raise these through [AtlasException]; the UI
 * only ever shows [userMessage], never a stack trace.
 */
sealed class AtlasFailure(val userMessage: String) {
    class InvalidEndpoint(detail: String) : AtlasFailure(detail)

    data object Timeout : AtlasFailure(
        "Atlas did not respond in time. Check that this phone is on the TurnHub-Atlas Wi-Fi.",
    )

    class Unreachable(detail: String?) : AtlasFailure(
        "Could not reach Atlas" + (detail?.let { " ($it)" } ?: "") +
            ". Check that this phone is on the TurnHub-Atlas Wi-Fi.",
    )

    class CleartextBlocked(host: String) : AtlasFailure(
        "Android only allows unencrypted HTTP to the Atlas access point (192.168.4.1), not $host.",
    )

    class HttpStatus(val code: Int, detail: String) : AtlasFailure("$detail (HTTP $code)")

    class NotTurnHub(detail: String) : AtlasFailure("$detail. Is this address a TurnHub Atlas?")

    class Incompatible(detail: String) : AtlasFailure("This Atlas is not compatible with this app: $detail")

    class Malformed(detail: String) : AtlasFailure("Atlas sent an unexpected response: $detail")

    class Unexpected(detail: String?) : AtlasFailure("Unexpected error" + (detail?.let { ": $it" } ?: ""))

    /** Polling kept failing, so the live view was dropped rather than shown stale. */
    class LostConnection(val cause: AtlasFailure) : AtlasFailure(
        "Lost connection to Atlas. ${cause.userMessage} Reconnect to load fresh state.",
    )
}

/** Carries an [AtlasFailure] through code that signals errors by throwing. */
class AtlasException(val failure: AtlasFailure) : Exception(failure.userMessage)
