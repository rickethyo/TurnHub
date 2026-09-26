package com.turnhub.android.ui.home

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.turnhub.android.data.AdminState
import com.turnhub.android.ui.components.AccentButton
import com.turnhub.android.ui.components.BrassCard
import com.turnhub.android.ui.components.Eyebrow
import com.turnhub.android.ui.components.ToneButton
import com.turnhub.android.ui.theme.palette
import kotlinx.coroutines.delay

/** The portal's Developer page: live activity feed, raw Atlas JSON and the serial log. */
@Composable
fun DevTab(admin: AdminState, actions: AdminActions) {
    val p = palette
    var live by rememberSaveable { mutableStateOf(true) }
    LaunchedEffect(live) {
        actions.onDeveloperRefresh()
        while (live) {
            delay(2_000)
            actions.onDeveloperRefresh()
        }
    }
    Column(verticalArrangement = Arrangement.spacedBy(14.dp)) {
        AdminMessage(admin, actions)
        BrassCard {
            Eyebrow("Developer")
            Text(
                "The serial log is Atlas's recent console output (secrets redacted). Sharing it hands the text to " +
                    "another app on this phone.",
                color = p.muted,
                style = MaterialTheme.typography.bodySmall,
            )
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                AccentButton("Share serial log", actions.onDownloadLog, Modifier.weight(1f))
                ToneButton("Refresh now", actions.onDeveloperRefresh, Modifier.weight(1f))
            }
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text("Live refresh (every 2 s)", color = p.text, modifier = Modifier.weight(1f))
                Switch(checked = live, onCheckedChange = { live = it })
            }
        }
        BrassCard {
            Eyebrow("Activity monitor")
            Text(
                "Recent in-memory Atlas events. The feed clears when Atlas restarts and is never written to flash.",
                color = p.muted,
                style = MaterialTheme.typography.bodySmall,
            )
            Column(
                Modifier
                    .fillMaxWidth()
                    .heightIn(max = 420.dp)
                    .clip(RoundedCornerShape(12.dp))
                    .background(p.inset)
                    .border(1.dp, p.line, RoundedCornerShape(12.dp))
                    .verticalScroll(rememberScrollState())
                    .padding(horizontal = 12.dp, vertical = 6.dp),
            ) {
                if (admin.activity.isEmpty()) Text("Waiting for activity…", color = p.faint, modifier = Modifier.padding(8.dp))
                admin.activity.forEach { event ->
                    Row(Modifier.padding(vertical = 5.dp), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                        Text("${event.ageMs / 100 / 10.0}s", color = p.faint, style = mono(), modifier = Modifier.width(56.dp))
                        Text(event.kind, color = if (p.dark) p.accentHi else p.accent, style = mono().copy(fontWeight = FontWeight.Bold))
                        Text(event.message, color = p.text, style = mono())
                    }
                }
            }
        }
        admin.devPanels.forEach { (name, json) ->
            BrassCard {
                Eyebrow(name.replaceFirstChar { it.uppercase() })
                Text(
                    json,
                    color = p.muted,
                    style = mono(),
                    modifier = Modifier
                        .fillMaxWidth()
                        .heightIn(max = 360.dp)
                        .clip(RoundedCornerShape(12.dp))
                        .background(p.inset)
                        .verticalScroll(rememberScrollState())
                        .horizontalScroll(rememberScrollState())
                        .padding(12.dp),
                )
            }
        }
    }
}

@Composable
private fun mono() = MaterialTheme.typography.bodySmall.copy(fontFamily = FontFamily.Monospace, fontSize = 12.sp)
