package com.turnhub.android.data

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Context
import android.content.Intent
import android.content.pm.ServiceInfo
import android.os.Build
import android.os.Handler
import android.os.IBinder
import android.os.Looper
import android.util.Log

/**
 * Keeps the Atlas Wi-Fi up through a quick app switch.
 *
 * Android releases a [android.net.wifi.WifiNetworkSpecifier] network the app
 * requested once the app stops being foreground, so switching to another app
 * for a moment used to drop the Atlas connection. While the app holds an
 * Atlas connection and leaves the screen, [MainActivity] starts this
 * short-lived `connectedDevice` foreground service: it keeps the app at
 * foreground-service importance, so the request (held by
 * [TargetedAtlasWifiLink] in the same process) stays granted. It stops itself
 * after [HOLD_MS], or as soon as the app returns. After that Android may drop
 * the network as before, and the app reconnects on the next Connect.
 *
 * It only holds; it never polls, plays or acts for the player.
 * *Needs verification* on hardware (the platform's background rule varies by
 * Android version).
 */
class AtlasLinkHoldService : Service() {

    private val handler = Handler(Looper.getMainLooper())
    private val stopper = Runnable { stopSelf() }

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        val notification = buildNotification()
        try {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                startForeground(NOTIFICATION_ID, notification, ServiceInfo.FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE)
            } else {
                startForeground(NOTIFICATION_ID, notification)
            }
        } catch (e: RuntimeException) {
            // e.g. ForegroundServiceStartNotAllowedException: nothing to hold with.
            Log.w(TAG, "Could not hold the Atlas link: ${e.javaClass.simpleName}: ${e.message}")
            stopSelf()
            return START_NOT_STICKY
        }
        handler.removeCallbacks(stopper)
        handler.postDelayed(stopper, HOLD_MS)
        return START_NOT_STICKY
    }

    override fun onDestroy() {
        handler.removeCallbacks(stopper)
        super.onDestroy()
    }

    private fun buildNotification(): Notification {
        val manager = getSystemService(NotificationManager::class.java)
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            manager.createNotificationChannel(
                NotificationChannel(CHANNEL_ID, "Atlas connection", NotificationManager.IMPORTANCE_LOW).apply {
                    description = "Shown while TurnHub keeps the Atlas Wi-Fi for a quick app switch"
                },
            )
        }
        val open = packageManager.getLaunchIntentForPackage(packageName)?.let {
            PendingIntent.getActivity(this, 0, it, PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT)
        }
        val builder = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            Notification.Builder(this, CHANNEL_ID)
        } else {
            @Suppress("DEPRECATION")
            Notification.Builder(this)
        }
        return builder
            .setSmallIcon(android.R.drawable.stat_sys_data_bluetooth)
            .setContentTitle("Keeping your table connection")
            .setContentText("TurnHub holds the Atlas Wi-Fi for ${HOLD_MS / 60_000} minutes. Come back to keep playing.")
            .setOngoing(true)
            .setContentIntent(open)
            .build()
    }

    companion object {
        private const val TAG = "AtlasLinkHold"
        private const val CHANNEL_ID = "atlas-link"
        private const val NOTIFICATION_ID = 17

        /** How long a backgrounded app keeps the Atlas Wi-Fi. */
        const val HOLD_MS = 2 * 60_000L

        /** Call when the app leaves the screen while connected. Safe to call repeatedly. */
        fun hold(context: Context) {
            try {
                val intent = Intent(context, AtlasLinkHoldService::class.java)
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) context.startForegroundService(intent)
                else context.startService(intent)
            } catch (e: RuntimeException) {
                Log.w(TAG, "Could not start the Atlas link hold: ${e.message}")
            }
        }

        /** Call when the app is back on screen. */
        fun release(context: Context) {
            context.stopService(Intent(context, AtlasLinkHoldService::class.java))
        }
    }
}
