package com.turnhub.android.data

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.content.Context
import android.os.Build
import android.os.Bundle
import com.turnhub.android.R
import com.turnhub.android.domain.LiveTurn
import com.turnhub.android.protocol.TableState

/**
 * The live turn notification (V1 phase 8): while TurnHub is in the background
 * and its player sits in a game, the lock screen and shade show whose turn it
 * is with a running turn clock (or the turn timer counting down). It alerts
 * (sound or vibration per the channel) only when the turn comes to this
 * player; other changes update it quietly. On Android 16+ it asks to be a
 * promoted "live update" so it stays at the top of the lock screen.
 *
 * Display only: Atlas owns the game, and the notification never acts for the
 * player. It shows only while the app keeps its Atlas connection
 * (AtlasLinkHoldService); when that ends, the notification goes too.
 */
class TurnNotifier(context: Context) {

    private val context = context.applicationContext
    private val manager = this.context.getSystemService(NotificationManager::class.java)
    private var shown: LiveTurn? = null

    fun allowed(): Boolean = manager?.areNotificationsEnabled() == true

    /** Shows or updates [turn]; null clears it. */
    fun show(turn: LiveTurn?) {
        val manager = manager ?: return
        if (turn == null) {
            clear()
            return
        }
        if (turn.sameMoment(shown)) return
        ensureChannel(manager)
        val becameMine = turn.mine && shown?.mine != true
        // A fresh post alerts; an update of the same notification stays quiet.
        if (becameMine) manager.cancel(NOTIFICATION_ID)
        manager.notify(NOTIFICATION_ID, build(turn, alert = becameMine))
        shown = turn
    }

    fun clear() {
        manager?.cancel(NOTIFICATION_ID)
        shown = null
    }

    private fun build(turn: LiveTurn, alert: Boolean): Notification {
        val open = context.packageManager.getLaunchIntentForPackage(context.packageName)?.let {
            PendingIntent.getActivity(context, 0, it, PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT)
        }
        val (title, text) = when (turn.state) {
            TableState.STARTING -> "Game starting" to "The first turn begins in a moment."
            TableState.PAUSED -> "Game paused" to (turn.activeName?.let { "$it was up." } ?: "The table is paused.")
            else -> (if (turn.mine) "Your turn" else "${turn.activeName ?: "Someone"}'s turn") to
                listOfNotNull(
                    turn.turnNumber?.let { "Turn $it" },
                    if (turn.timerEndsAtMs != null) "time left" else if (turn.turnStartedAtMs != null) "turn time" else null,
                ).joinToString(" · ")
        }
        val builder = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            Notification.Builder(context, CHANNEL_ID)
        } else {
            @Suppress("DEPRECATION")
            Notification.Builder(context)
        }
        builder
            .setSmallIcon(R.drawable.ic_th_timer)
            .setContentTitle(title)
            .setContentText(text)
            .setCategory(Notification.CATEGORY_STATUS)
            .setVisibility(Notification.VISIBILITY_PUBLIC)
            .setOngoing(true)
            .setOnlyAlertOnce(!alert)
            .setShowWhen(true)
            .setContentIntent(open)
        when {
            turn.timerEndsAtMs != null -> builder
                .setWhen(turn.timerEndsAtMs)
                .setUsesChronometer(true)
                .setChronometerCountDown(true)
            turn.turnStartedAtMs != null -> builder
                .setWhen(turn.turnStartedAtMs)
                .setUsesChronometer(true)
            else -> builder.setShowWhen(false)
        }
        // Android 16+: ask for a promoted ongoing ("live update") notification.
        builder.addExtras(Bundle().apply { putBoolean(EXTRA_REQUEST_PROMOTED_ONGOING, true) })
        return builder.build()
    }

    private fun ensureChannel(manager: NotificationManager) {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.O) return
        if (manager.getNotificationChannel(CHANNEL_ID) != null) return
        manager.createNotificationChannel(
            NotificationChannel(CHANNEL_ID, "Turns", NotificationManager.IMPORTANCE_DEFAULT).apply {
                description = "Whose turn it is and the turn clock, while TurnHub is in the background. " +
                    "Alerts when the turn comes to you."
                enableVibration(true)
                setShowBadge(false)
                lockscreenVisibility = Notification.VISIBILITY_PUBLIC
            },
        )
    }

    private companion object {
        const val CHANNEL_ID = "turns"
        const val NOTIFICATION_ID = 3
        /** Notification.EXTRA_REQUEST_PROMOTED_ONGOING (Android 16), by value so older SDKs ignore it. */
        const val EXTRA_REQUEST_PROMOTED_ONGOING = "android.requestPromotedOngoing"
    }
}
