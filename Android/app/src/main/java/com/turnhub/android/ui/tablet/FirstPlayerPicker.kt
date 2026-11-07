package com.turnhub.android.ui.tablet

import com.turnhub.android.ui.components.ActionRole

import com.turnhub.android.ui.components.ActionButton

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawingPadding
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import com.turnhub.android.domain.TablePlayer
import com.turnhub.android.ui.components.AccentButton
import com.turnhub.android.ui.components.colorInk
import com.turnhub.android.ui.components.playerColor
import com.turnhub.android.ui.theme.palette

/**
 * Start's first step: who goes first, asked full screen at the table instead of with a
 * chip on every setup row. Each player is a tile in their table color; Random leaves it to the app.
 */
@OptIn(ExperimentalLayoutApi::class)
@Composable
internal fun FirstPlayerPicker(players: List<TablePlayer>, teams: Boolean, onPick: (TablePlayer?) -> Unit, onDismiss: () -> Unit) {
    val p = palette
    Dialog(onDismiss, DialogProperties(usePlatformDefaultWidth = false)) {
        Box(Modifier.fillMaxSize().background(p.bg).safeDrawingPadding(), contentAlignment = Alignment.Center) {
            Column(Modifier.widthIn(max = 640.dp).fillMaxWidth().verticalScroll(rememberScrollState()).padding(20.dp),
                verticalArrangement = Arrangement.spacedBy(16.dp), horizontalAlignment = Alignment.CenterHorizontally) {
                Text("Who goes first?", color = p.text, style = MaterialTheme.typography.headlineMedium,
                    fontWeight = FontWeight.Bold, modifier = Modifier.semantics { heading() })
                if (teams) Text("Their team takes the first turn.", color = p.muted, style = MaterialTheme.typography.bodyMedium)
                FlowRow(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(10.dp),
                    verticalArrangement = Arrangement.spacedBy(10.dp), maxItemsInEachRow = 2) {
                    (players.map { it as TablePlayer? } + listOf(null)).forEach { player ->
                        val label = player?.label ?: "Random"
                        val hue = player?.let { playerColor(it.color, p.avatarColor(it.playerNumber)) } ?: p.lineStrong
                        val shape = RoundedCornerShape(18.dp)
                        Box(Modifier.weight(1f).heightIn(min = 96.dp).clip(shape).background(p.surface2)
                            .border(2.dp, hue, shape)
                            .clickable(onClickLabel = if (player == null) "Choose a random first player" else "$label goes first", role = Role.Button) { onPick(player) }
                            .padding(12.dp), contentAlignment = Alignment.Center) {
                            Text(label, color = p.text, style = MaterialTheme.typography.titleLarge,
                                fontWeight = FontWeight.SemiBold, maxLines = 2, overflow = TextOverflow.Ellipsis, textAlign = TextAlign.Center)
                        }
                    }
                }
                ActionButton(onClick = onDismiss, role = ActionRole.SECONDARY) { Text("Back to setup", color = p.muted) }
            }
        }
    }
}
