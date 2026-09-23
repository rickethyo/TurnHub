package com.turnhub.android.ui.components

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.dp
import com.turnhub.android.protocol.AtlasConnectionState

/**
 * Renders Atlas connection state as text plus a color dot -- never color alone,
 * per Documentation/engineering/ARCHITECTURAL_INVARIANTS.md Invariant 11
 * ("Color MUST NOT be the sole carrier of essential meaning").
 */
@Composable
fun ConnectionStateBadge(
    state: AtlasConnectionState,
    modifier: Modifier = Modifier,
) {
    val (label, color) = when (state) {
        AtlasConnectionState.DISCONNECTED -> "Disconnected" to MaterialTheme.colorScheme.error
        AtlasConnectionState.CONNECTING -> "Connecting…" to Color(0xFFB26A00)
        AtlasConnectionState.CONNECTED -> "Connected" to Color(0xFF2E7D32)
    }

    Row(
        modifier = modifier
            .background(MaterialTheme.colorScheme.surfaceVariant, RoundedCornerShape(percent = 50))
            .padding(horizontal = 12.dp, vertical = 6.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Box(
            modifier = Modifier
                .size(10.dp)
                .background(color, CircleShape),
        )
        Spacer(modifier = Modifier.width(8.dp))
        Text(text = label, style = MaterialTheme.typography.labelLarge)
    }
}
