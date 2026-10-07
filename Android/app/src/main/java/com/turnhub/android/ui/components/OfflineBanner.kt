package com.turnhub.android.ui.components

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.semantics.LiveRegionMode
import androidx.compose.ui.semantics.liveRegion
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.dp
import com.turnhub.android.ui.theme.palette

/**
 * Shown while Atlas isn't answering: the table on screen is the last state
 * Atlas sent ([lastUpdateMs] is when it arrived, on [com.turnhub.android.domain.TableClock]),
 * nothing can be changed, and the app keeps reconnecting.
 */
@Composable
fun OfflineBanner(lastUpdateMs: Long, modifier: Modifier = Modifier) {
    val p = palette
    val now = rememberNowMs(ticking = true, intervalMs = 1_000)
    val shape = RoundedCornerShape(12.dp)
    Column(
        modifier
            .clip(shape)
            .background(p.surface2)
            .border(1.dp, p.warn, shape)
            .padding(horizontal = 14.dp, vertical = 10.dp),
    ) {
        // Announced once when it appears; the age line below ticks quietly.
        Text("Offline: reconnecting to Atlas", color = p.warn, style = MaterialTheme.typography.titleSmall,
            modifier = Modifier.semantics { liveRegion = LiveRegionMode.Polite })
        Text(
            "Showing the table as of ${offlineAge(now - lastUpdateMs)}. Changes are paused until Atlas answers.",
            color = p.text,
            style = MaterialTheme.typography.bodyMedium,
        )
    }
}

/** "a moment ago", "40 s ago", "3 min ago". */
fun offlineAge(ms: Long): String {
    val seconds = ms.coerceAtLeast(0) / 1000
    return when {
        seconds < 5 -> "a moment ago"
        seconds < 60 -> "$seconds s ago"
        seconds < 3600 -> "${seconds / 60} min ago"
        else -> "${seconds / 3600} h ago"
    }
}
