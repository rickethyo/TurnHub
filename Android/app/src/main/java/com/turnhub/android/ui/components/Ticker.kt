package com.turnhub.android.ui.components

import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableLongStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import com.turnhub.android.domain.TableClock
import kotlinx.coroutines.delay

/**
 * The local monotonic time, refreshed every [intervalMs] while [ticking]. Used
 * with [TableClock] to render Atlas clocks smoothly between polls; when not
 * ticking (paused, lobby, game over) it stops, so nothing appears to advance.
 */
@Composable
fun rememberNowMs(ticking: Boolean, intervalMs: Long = 250): Long {
    var now by remember { mutableLongStateOf(TableClock.nowMs()) }
    LaunchedEffect(ticking) {
        now = TableClock.nowMs()
        while (ticking) {
            delay(intervalMs)
            now = TableClock.nowMs()
        }
    }
    return now
}
