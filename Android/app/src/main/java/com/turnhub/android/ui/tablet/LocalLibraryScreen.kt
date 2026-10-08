package com.turnhub.android.ui.tablet

import androidx.activity.compose.BackHandler
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawingPadding
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.dp
import com.turnhub.android.standalone.DeliveryStatus
import com.turnhub.android.standalone.StandaloneState
import com.turnhub.android.ui.components.BrassCard
import com.turnhub.android.ui.components.tableBackground
import com.turnhub.android.ui.theme.palette
import java.text.DateFormat
import java.util.Date

/** Local facts and delivery states are presented separately, with text meaning. */
@Composable
fun LocalLibraryScreen(state: StandaloneState, onClose: () -> Unit) {
    val p = palette
    BackHandler(onBack = onClose)
    Box(Modifier.fillMaxSize().tableBackground(p).safeDrawingPadding(), contentAlignment = Alignment.TopCenter) {
        LazyColumn(Modifier.widthIn(max = 720.dp).fillMaxWidth().padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(14.dp)) {
            item {
                Text("Device players and history", color = p.text, style = MaterialTheme.typography.headlineSmall,
                    modifier = Modifier.semantics { heading() })
                TextButton(onClick = onClose) { Text("Back") }
                Text("Results stay here after import. No games are removed automatically. Clearing app data or uninstalling removes them; backup/export is not available yet.", color = p.muted)
                state.storageProblem?.let { Text(it, color = p.text) }
            }
            item { Text("Local players", color = p.text, modifier = Modifier.semantics { heading() }) }
            if (state.library.players.isEmpty()) item { Text("Add players in Play on this device. No account is needed.", color = p.muted) }
            items(state.library.players, key = { "player-${it.localId}" }) { player ->
                val totals = state.library.totals(player.localId)
                BrassCard {
                    Text("${player.name} · ${player.localId.take(6)}", color = p.text)
                    Text("${totals.played} played · ${totals.won} won · ${totals.draws} draws", color = p.muted)
                }
            }
            item { Text("Finished games", color = p.text, modifier = Modifier.semantics { heading() }) }
            if (state.library.history.isEmpty()) item { Text("Finish a game to see its result here.", color = p.muted) }
            items(state.library.history.asReversed(), key = { "match-${it.recordId}" }) { record ->
                val delivery = state.library.delivery(record.recordId)
                BrassCard {
                    Text(DateFormat.getDateTimeInstance(DateFormat.MEDIUM, DateFormat.SHORT).format(Date(record.startedAtMs)), color = p.text)
                    Text("${record.profile.wireValue} · ${record.durationMs / 60_000} minutes · ${record.startingLife} starting life", color = p.muted)
                    Text(record.winner?.let { "Winner: ${record.players[it].name}" } ?: "Draw / no winner", color = p.text)
                    record.players.forEach { player ->
                        Text("${player.name}${player.localId?.let { " · ${it.take(6)}" } ?: " · legacy identity"}: ${player.finalLife} life, ${player.turnsCompleted} completed turns", color = p.muted)
                    }
                    val status = when (delivery.status) {
                        DeliveryStatus.NEEDS_LINKING -> "Local result · Atlas import needs explicit linking"
                        DeliveryStatus.PENDING -> "Atlas import pending"
                        DeliveryStatus.IMPORTED -> "Imported to Atlas"
                        DeliveryStatus.REJECTED -> "Atlas import needs attention"
                    }
                    Text(status, color = p.text)
                    delivery.reason?.let { Text(it, color = p.muted) }
                    Text("Match ${record.recordId}", color = p.faint, style = MaterialTheme.typography.bodySmall)
                }
            }
        }
    }
}
