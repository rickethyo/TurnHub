package com.turnhub.android.data

import android.content.Context
import android.net.ConnectivityManager
import android.net.Network
import android.net.NetworkCapabilities
import java.net.HttpURLConnection
import java.net.URL

/**
 * Opens Atlas requests on the phone's current Wi-Fi network when there is one.
 *
 * Atlas's access point has no internet. With mobile data on, Android usually
 * keeps cellular as the default network, so an unbound request to
 * 192.168.4.1 would leave over cellular and never reach Atlas. Binding just
 * these connections to the Wi-Fi the user already joined avoids that without
 * changing the process-wide default network or automating Wi-Fi in any way.
 * Falls back to the default route when no Wi-Fi network is present.
 */
class WifiPreferringConnectionOpener(context: Context) : HttpConnectionOpener {

    private val connectivity: ConnectivityManager? =
        context.applicationContext.getSystemService(ConnectivityManager::class.java)

    override fun open(url: URL): HttpURLConnection =
        (wifiNetwork()?.openConnection(url) ?: url.openConnection()) as HttpURLConnection

    // allNetworks is deprecated in API 31 in favor of network callbacks, which
    // would add lifecycle state for no benefit to a single lookup per request.
    @Suppress("DEPRECATION")
    private fun wifiNetwork(): Network? {
        val manager = connectivity ?: return null
        return manager.allNetworks.firstOrNull { network ->
            manager.getNetworkCapabilities(network)
                ?.hasTransport(NetworkCapabilities.TRANSPORT_WIFI) == true
        }
    }
}
