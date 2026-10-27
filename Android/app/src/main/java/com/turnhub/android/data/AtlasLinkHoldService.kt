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
import android.os.PowerManager
import android.net.wifi.WifiManager
import android.util.Log

/**
 * Keeps the game connection up through a quick app switch or a screen lock.
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
 * The same hold covers a game shared over Wi-Fi ([com.turnhub.android.lan]):
 * a host keeps answering its guests ([active]) and a guest keeps its seat.
 * While it runs it also holds a Wi-Fi lock and a partial wake lock, both
 * bounded by [HOLD_MS], so a locked screen doesn't put the radio or CPU to
 * sleep between requests.
 *
 * It only holds; it never polls, plays or acts for the player.
 * *Needs verification* on hardware (the platform's background rule varies by
 * Android version).
 */
class AtlasLinkHoldService : Service() {

    private val handler = Handler(Looper.getMainLooper())
    private val stopper = Runnable { stopSelf() }
    private var wifiLock: WifiManager.WifiLock? = null
    private var wakeLock: PowerManager.WakeLock? = null

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
        active = true
        acquireLocks()
        handler.removeCallbacks(stopper)
        handler.postDelayed(stopper, HOLD_MS)
        return START_NOT_STICKY
    }

    override fun onDestroy() {
        active = false
        handler.removeCallbacks(stopper)
        releaseLocks()
        super.onDestroy()
    }

    /** Re-acquired on each start, so a repeated hold restarts the timeout too. */
    private fun acquireLocks() {
        releaseLocks()
        try {
            @Suppress("DEPRECATION")
            wifiLock = applicationContext.getSystemService(WifiManager::class.java)
                ?.createWifiLock(WifiManager.WIFI_MODE_FULL_HIGH_PERF, "TurnHub:game-link")
                ?.apply { setReferenceCounted(false); acquire() }
            wakeLock = getSystemService(PowerManager::class.java)
                ?.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "TurnHub:game-link")
                ?.apply { setReferenceCounted(false); acquire(HOLD_MS + 5_000) }
        } catch (e: RuntimeException) {
            // The foreground service alone still keeps the app eligible.
            Log.w(TAG, "Could not take the game link locks: ${e.message}")
        }
    }

    private fun releaseLocks() {
        wifiLock?.let { if (it.isHeld) runCatching { it.release() } }
        wakeLock?.let { if (it.isHeld) runCatching { it.release() } }
        wifiLock = null
        wakeLock = null
    }

    private fun buildNotification(): Notification {
        val manager = getSystemService(NotificationManager::class.java)
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            manager.createNotificationChannel(
                NotificationChannel(CHANNEL_ID, "Table connection", NotificationManager.IMPORTANCE_LOW).apply {
                    description = "Shown while TurnHub keeps your game connected for a quick app switch"
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
            .setContentText("TurnHub keeps your game connected for ${HOLD_MS / 60_000} minutes. Come back to keep playing.")
            .setOngoing(true)
            .setContentIntent(open)
            .build()
    }

    companion object {
        private const val TAG = "AtlasLinkHold"
        private const val CHANNEL_ID = "atlas-link"
        private const val NOTIFICATION_ID = 17

        /** How long a backgrounded app keeps its game connection. */
        const val HOLD_MS = 5 * 60_000L

        /** True while the hold runs: a backgrounded host keeps serving its shared game. */
        @Volatile
        var active = false
            private set

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
