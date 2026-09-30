package com.turnhub.android.data

import android.Manifest
import android.content.Context
import android.content.pm.PackageManager
import android.net.wifi.WifiManager
import android.os.Build
import kotlinx.coroutines.delay

/**
 * [AtlasScanner] from Android's Wi-Fi scan results. Needs Android 13+ and the
 * NEARBY_WIFI_DEVICES permission, declared `neverForLocation`: the app only
 * looks for TurnHub network names and never derives location. Older Android
 * would need location permission for this, so there it reports null and the
 * app waits for the user to tap Connect.
 */
class WifiScanAtlasScanner(
    context: Context,
    /** Time for a fresh scan to land; cached results are used either way. */
    private val settleMs: Long = 2_500,
) : AtlasScanner {

    private val appContext = context.applicationContext
    private val wifi: WifiManager? = appContext.getSystemService(WifiManager::class.java)

    override suspend fun visibleAtlasNetworks(): Set<String>? {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.TIRAMISU) return null
        if (appContext.checkSelfPermission(Manifest.permission.NEARBY_WIFI_DEVICES) != PackageManager.PERMISSION_GRANTED) {
            return null
        }
        val manager = wifi ?: return null
        if (!manager.isWifiEnabled) return emptySet()
        // Android throttles foreground scans (four per two minutes); a refused
        // scan still leaves the system's own recent results to read.
        @Suppress("DEPRECATION")
        val started = try {
            manager.startScan()
        } catch (_: SecurityException) {
            false
        }
        if (started) delay(settleMs)
        return try {
            manager.scanResults
                .mapNotNull { it.wifiSsid?.toString()?.removeSurrounding("\"") }
                .filter { it.startsWith(AtlasScanner.SSID_PREFIX) }
                .toSet()
        } catch (_: SecurityException) {
            null
        }
    }
}
