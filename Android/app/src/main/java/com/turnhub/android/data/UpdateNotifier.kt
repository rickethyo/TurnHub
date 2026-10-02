package com.turnhub.android.data

import android.Manifest
import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.content.Context
import android.content.pm.PackageManager
import android.os.Build

/**
 * The notification-bar "Update available" (the in-app card and the Atlas and
 * Sigil screens say the same). It appears when the number of devices behind
 * rises while the app is in the background, and goes away when it falls to
 * zero. Information only: tapping it opens the app, whose card installs.
 */
class UpdateNotifier(private val context: Context) {

    /** True when a notification would be shown: Android 13+ needs the user's permission. */
    fun allowed(): Boolean =
        Build.VERSION.SDK_INT < Build.VERSION_CODES.TIRAMISU ||
            context.checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) == PackageManager.PERMISSION_GRANTED

    fun show(devices: Int) {
        if (!allowed()) return
        val manager = context.getSystemService(NotificationManager::class.java)
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            manager.createNotificationChannel(
                NotificationChannel(CHANNEL_ID, "Firmware updates", NotificationManager.IMPORTANCE_DEFAULT).apply {
                    description = "Tells you when newer TurnHub firmware is available for Atlas or a Sigil"
                },
            )
        }
        val open = context.packageManager.getLaunchIntentForPackage(context.packageName)?.let {
            PendingIntent.getActivity(context, 1, it, PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT)
        }
        val builder = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            Notification.Builder(context, CHANNEL_ID)
        } else {
            @Suppress("DEPRECATION")
            Notification.Builder(context)
        }
        builder
            .setSmallIcon(android.R.drawable.stat_sys_download_done)
            .setContentTitle("Update available")
            .setContentText(
                if (devices == 1) "Newer firmware is ready for 1 device at your table. Open TurnHub to update."
                else "Newer firmware is ready for $devices devices at your table. Open TurnHub to update.",
            )
            .setAutoCancel(true)
            .setOnlyAlertOnce(true)
        open?.let { builder.setContentIntent(it) }
        manager.notify(NOTIFICATION_ID, builder.build())
    }

    fun clear() {
        context.getSystemService(NotificationManager::class.java).cancel(NOTIFICATION_ID)
    }

    companion object {
        const val CHANNEL_ID = "turnhub_updates"
        const val NOTIFICATION_ID = 2

        /** Notify when more devices are behind than when last told; never for none. */
        fun shouldNotify(lastNotified: Int, now: Int): Boolean = now > 0 && now > lastNotified
    }
}
