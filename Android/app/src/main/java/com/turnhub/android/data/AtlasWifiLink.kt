package com.turnhub.android.data

import kotlinx.coroutines.flow.StateFlow

/** The Atlas Wi-Fi network to join. [passphrase] is WPA2 (8-63 characters). */
data class WifiCredentials(val ssid: String, val passphrase: String) {
    override fun toString(): String = "WifiCredentials(ssid=$ssid, passphrase=***)"

    companion object {
        /** Atlas's standalone access point name (Atlas/include/config.h). */
        const val DEFAULT_ATLAS_SSID = "TurnHub-Atlas"

        /**
         * The shipped pre-setup passphrase a new or factory-reset Atlas uses
         * until its owner sets one (AtlasConfig::WIFI_DEFAULT_PASSWORD,
         * protocol/http-v1.md). Public by design; keep in sync with firmware.
         */
        const val DEFAULT_ATLAS_PASSPHRASE = "TurnHub-Setup"

        /** WPA2-PSK passphrase length, as Atlas's portal also enforces. */
        val PASSPHRASE_LENGTH = 8..63
    }
}

sealed interface WifiJoinResult {
    data object Joined : WifiJoinResult

    /**
     * Android could not provide the network: the user declined the system
     * dialog, the network was not found, or the password was wrong. Android
     * does not say which.
     */
    data object Unavailable : WifiJoinResult

    data class Failed(val detail: String?) : WifiJoinResult
}

/**
 * Joins the phone to an Atlas access point for this app only, without the user
 * visiting Wi-Fi settings (Android's targeted network request). The joined
 * network carries only TurnHub's Atlas traffic; the rest of the phone keeps its
 * normal connection. Credentials come from the caller, so the same link can
 * later join an OOBE setup network.
 */
interface AtlasWifiLink {
    /** SSID currently joined through this link, or null. */
    val joinedSsid: StateFlow<String?>

    /** Asks Android for [credentials]' network; replaces any previous join. */
    suspend fun join(credentials: WifiCredentials): WifiJoinResult

    /** Gives the network back; Android returns the phone to its usual Wi-Fi. */
    fun release()
}

/** Remembers Atlas Wi-Fi passwords that have successfully joined. */
interface WifiCredentialStore {
    /** The SSID the user last joined, used as the prompt's default. */
    fun lastSsid(): String?

    fun load(ssid: String): WifiCredentials?

    fun save(credentials: WifiCredentials)
}
