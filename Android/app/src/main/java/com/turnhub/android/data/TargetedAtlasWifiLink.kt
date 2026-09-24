package com.turnhub.android.data

import android.content.Context
import android.net.ConnectivityManager
import android.net.Network
import android.net.NetworkCapabilities
import android.net.NetworkRequest
import android.net.wifi.WifiNetworkSpecifier
import android.os.Build
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.suspendCancellableCoroutine
import java.net.HttpURLConnection
import java.net.URL
import kotlin.coroutines.resume

/**
 * [AtlasWifiLink] using Android's targeted Wi-Fi request (`WifiNetworkSpecifier`
 * + `ConnectivityManager.requestNetwork`). Android shows its own "connect to
 * device" dialog the first time and remembers the approval for that access
 * point afterwards.
 *
 * Also the [HttpConnectionOpener] for Atlas requests: while a network is
 * joined, connections are opened on it; otherwise they fall back to
 * [fallback] (the Wi-Fi the user joined manually, if any).
 *
 * The request must stay registered for the network to stay up, so the
 * callback is held until [release].
 */
class TargetedAtlasWifiLink(
    context: Context,
    private val fallback: HttpConnectionOpener = WifiPreferringConnectionOpener(context),
    /** Covers scanning, the user's approval and association. */
    private val joinTimeoutMs: Int = 45_000,
) : AtlasWifiLink, HttpConnectionOpener {

    private val connectivity: ConnectivityManager =
        context.applicationContext.getSystemService(ConnectivityManager::class.java)

    private val lock = Any()
    private var callback: ConnectivityManager.NetworkCallback? = null

    @Volatile
    private var network: Network? = null

    private val _joinedSsid = MutableStateFlow<String?>(null)
    override val joinedSsid: StateFlow<String?> = _joinedSsid.asStateFlow()

    override fun open(url: URL): HttpURLConnection =
        (network?.openConnection(url) ?: return fallback.open(url)) as HttpURLConnection

    override suspend fun join(credentials: WifiCredentials): WifiJoinResult {
        release()
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.Q) {
            return WifiJoinResult.Failed(
                "joining automatically needs Android 10 or newer. Join ${credentials.ssid} in " +
                    "Wi-Fi settings, then choose \"I've joined this Wi-Fi already\"",
            )
        }
        val request = try {
            val specifier = WifiNetworkSpecifier.Builder()
                .setSsid(credentials.ssid)
                .setWpa2Passphrase(credentials.passphrase)
                .build()
            NetworkRequest.Builder()
                .addTransportType(NetworkCapabilities.TRANSPORT_WIFI)
                .removeCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET)
                .setNetworkSpecifier(specifier)
                .build()
        } catch (e: IllegalArgumentException) {
            return WifiJoinResult.Failed(e.message)
        }

        return suspendCancellableCoroutine { continuation ->
            val networkCallback = object : ConnectivityManager.NetworkCallback() {
                override fun onAvailable(available: Network) {
                    network = available
                    _joinedSsid.value = credentials.ssid
                    if (continuation.isActive) continuation.resume(WifiJoinResult.Joined)
                }

                override fun onUnavailable() {
                    // Declined, not found, wrong password or timed out; Android
                    // has already dropped this request.
                    synchronized(lock) { if (callback === this) callback = null }
                    if (continuation.isActive) continuation.resume(WifiJoinResult.Unavailable)
                }

                override fun onLost(lost: Network) {
                    if (network == lost) {
                        network = null
                        _joinedSsid.value = null
                    }
                }
            }
            synchronized(lock) { callback = networkCallback }
            continuation.invokeOnCancellation { release() }
            try {
                connectivity.requestNetwork(request, networkCallback, joinTimeoutMs)
            } catch (e: RuntimeException) {
                // SecurityException, TooManyRequestsException, ...
                synchronized(lock) { if (callback === networkCallback) callback = null }
                if (continuation.isActive) {
                    continuation.resume(WifiJoinResult.Failed("${e.javaClass.simpleName}: ${e.message}"))
                }
            }
        }
    }

    override fun release() {
        val ending = synchronized(lock) { callback.also { callback = null } }
        network = null
        _joinedSsid.value = null
        if (ending != null) {
            try {
                connectivity.unregisterNetworkCallback(ending)
            } catch (_: IllegalArgumentException) {
                // Already unregistered by the platform (e.g. after onUnavailable).
            }
        }
    }
}
