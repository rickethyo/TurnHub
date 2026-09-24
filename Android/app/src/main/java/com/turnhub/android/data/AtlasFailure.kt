package com.turnhub.android.data

/**
 * Why talking to an Atlas failed, phrased for the person holding the phone.
 * Transports and the repository raise these through [AtlasException]; the UI
 * shows [userMessage], plus [technicalDetail] (the underlying exception, if
 * any) in small print so field failures can be diagnosed without a debugger.
 */
sealed class AtlasFailure(val userMessage: String, val technicalDetail: String? = null) {
    class InvalidEndpoint(detail: String) : AtlasFailure(detail)

    /** The app lacks Android 17's local-network ("Nearby devices") permission. */
    data object LocalNetworkPermissionDenied : AtlasFailure(
        "TurnHub needs the Nearby devices permission to reach Atlas on the local Wi-Fi. " +
            "Allow it when asked, or in Settings › Apps › TurnHub › Permissions › Nearby devices.",
    )

    class Timeout(technicalDetail: String? = null) : AtlasFailure(
        "Atlas did not respond in time. Check that this phone is on the TurnHub-Atlas Wi-Fi.",
        technicalDetail,
    )

    class Unreachable(technicalDetail: String?) : AtlasFailure(
        "Could not reach Atlas. Check that this phone is on the TurnHub-Atlas Wi-Fi.",
        technicalDetail,
    )

    class CleartextBlocked(host: String) : AtlasFailure(
        "Android only allows unencrypted HTTP to the Atlas access point (192.168.4.1), not $host.",
    )

    class HttpStatus(val code: Int, detail: String) : AtlasFailure("$detail (HTTP $code)")

    class NotTurnHub(detail: String) : AtlasFailure("$detail. Is this address a TurnHub Atlas?")

    class Incompatible(detail: String) : AtlasFailure("This Atlas is not compatible with this app: $detail")

    class Malformed(detail: String) : AtlasFailure("Atlas sent an unexpected response: $detail")

    class Unexpected(technicalDetail: String?) : AtlasFailure("Unexpected error.", technicalDetail)

    /** Polling kept failing, so the live view was dropped rather than shown stale. */
    class LostConnection(val cause: AtlasFailure) : AtlasFailure(
        "Lost connection to Atlas. ${cause.userMessage} Reconnect to load fresh state.",
        cause.technicalDetail,
    )
}

/** Carries an [AtlasFailure] through code that signals errors by throwing. */
class AtlasException(val failure: AtlasFailure) : Exception(failure.userMessage)
