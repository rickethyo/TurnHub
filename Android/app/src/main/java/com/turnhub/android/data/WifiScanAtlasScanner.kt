package com.turnhub.android.data

import android.Manifest
import android.content.Context
import android.content.pm.PackageManager
import android.net.wifi.WifiManager
import android.os.Build
import android.util.Log
import kotlinx.coroutines.delay

/**
 * [AtlasScanner] from Android's Wi-Fi scan results. Needs Android 13+ and the
 * NEARBY_WIFI_DEVICES permission, declared `neverForLocation`: the app only
 * looks for TurnHub network names and never derives location. Older Android
 * would need location permission for this, so there it reports null and the
 * app waits for the user to tap Connect.
 *
 * Every scan logs one `TurnHubScan` line (logcat), so a table the app didn't
 * see can be diagnosed on the phone.
 */
class WifiScanAtlasScanner(
    context: Context,
    /** Time for a fresh scan to land; cached results are used either way. */
    private val settleMs: Long = 2_500,
) : AtlasScanner {

    private val appContext = context.applicationContext
    private val wifi: WifiManager? = appContext.getSystemService(WifiManager::class.java)

    override suspend fun visibleAtlasNetworks(): Set<String>? {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.TIRAMISU) return null.also { log("unavailable: Android ${Build.VERSION.SDK_INT}") }
        if (appContext.checkSelfPermission(Manifest.permission.NEARBY_WIFI_DEVICES) != PackageManager.PERMISSION_GRANTED) {
            return null.also { log("unavailable: NEARBY_WIFI_DEVICES not granted") }
        }
        val manager = wifi ?: return null.also { log("unavailable: no WifiManager") }
        if (!manager.isWifiEnabled) return emptySet<String>().also { log("Wi-Fi is off") }
        // Android throttles foreground scans (four per two minutes); a refused
        // scan still leaves the system's own recent results to read.
        @Suppress("DEPRECATION")
        val started = try {
            manager.startScan()
        } catch (e: SecurityException) {
            log("startScan refused: ${e.message}")
            false
        }
        if (started) delay(settleMs)
        return try {
            val results = manager.scanResults
            val names = results.mapNotNull { it.wifiSsid?.toString()?.removeSurrounding("\"") }
            val atlases = names.filter { it.startsWith(AtlasScanner.SSID_PREFIX) }.toSet()
            log("started=$started results=${results.size} atlas=$atlases")
            // No networks at all means the scan is blind (location services
            // off, a refused scan with an empty cache), not an empty room:
            // report "can't tell" so the saved table is still tried.
            if (results.isEmpty()) null else atlases
        } catch (e: SecurityException) {
            log("scanResults refused: ${e.message}")
            null
        }
    }

    private fun log(message: String) {
        Log.i(TAG, message)
    }

    private companion object {
        const val TAG = "TurnHubScan"
    }
}
