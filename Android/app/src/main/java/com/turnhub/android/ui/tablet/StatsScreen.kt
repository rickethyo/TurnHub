package com.turnhub.android.ui.tablet

import androidx.activity.compose.BackHandler
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawingPadding
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.itemsIndexed
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.stateDescription
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.turnhub.android.domain.TableClock
import com.turnhub.android.standalone.StandaloneState
import com.turnhub.android.standalone.leaderboard
import com.turnhub.android.standalone.ordinal
import com.turnhub.android.standalone.statsShareText
import com.turnhub.android.ui.components.BrassCard
import com.turnhub.android.ui.components.ToneButton
import com.turnhub.android.ui.components.tableBackground
import com.turnhub.android.ui.theme.palette

/**
 * Everyone's results from games played on this device, open to the whole
 * table: no account or PIN is needed to read them. Account settings stay in
 * Players and history.
 */
@Composable
fun StatsScreen(
    state: StandaloneState,
    onClose: () -> Unit,
    onShareStats: (String) -> Unit,
) {
    val p = palette
    BackHandler(onBack = onClose)
    val rows = state.library.leaderboard()
    Box(Modifier.fillMaxSize().tableBackground(p).safeDrawingPadding(), contentAlignment = Alignment.TopCenter) {
        LazyColumn(Modifier.widthIn(max = 720.dp).fillMaxWidth().padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp)) {
            item {
                Text("Stats", color = p.text, style = MaterialTheme.typography.headlineSmall,
                    modifier = Modifier.semantics { heading() })
                TextButton(onClick = onClose) { Text("Back") }
                Text("Every table has its legends.", color = p.muted, style = MaterialTheme.typography.bodySmall)
                Text("On this device · Most wins first", color = p.muted, style = MaterialTheme.typography.labelMedium)
            }
            if (rows.isEmpty()) item { Text("Finish a game with saved players to see stats here.", color = p.muted) }
            itemsIndexed(rows, key = { _, row -> row.player.localId }) { index, row ->
                val stats = row.stats
                val totals = stats.totals
                var open by rememberSaveable(row.player.localId) { mutableStateOf(false) }
                BrassCard {
                    Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                        Text(ordinal(index + 1), color = p.muted)
                        Column(Modifier.weight(1f)) {
                            Text(row.player.name, color = p.text, fontWeight = FontWeight.SemiBold)
                        }
                    }
                    Text("${totals.won} wins · ${totals.played} played · ${row.winPercent}% won" +
                        if (totals.draws > 0) " · ${totals.draws} draws" else "", color = p.muted)
                    TextButton(onClick = { open = !open }, modifier = Modifier.semantics {
                        stateDescription = if (open) "Expanded" else "Collapsed"
                    }) { Text(if (open) "Hide details" else "View details") }
                    if (open) {
                        Text("Player ID: ${row.player.profileId}", color = p.muted, style = MaterialTheme.typography.bodySmall)
                        val f = TableClock::format
                        Text("Went first ${stats.started} · knocked out ${stats.eliminated} · conceded ${stats.conceded}", color = p.muted)
                        Text("${stats.turnsCompleted} turns · average ${f(stats.averageTurnMs)} · fastest ${f(stats.fastestTurnMs)} · longest ${f(stats.longestTurnMs)}", color = p.muted)
                        Text("Opponents eliminated ${stats.eliminationsMade}", color = p.muted)
                        Text("Average game ${f(stats.averageGameMs)}", color = p.muted)
                        if (stats.starPlayed > 0) Text("Star Commander: ${stats.starPlayed} played · ${stats.starWon} won", color = p.muted)
                        val full = state.library.profileStatistics(row.player.localId)
                        Text("Total game time ${f(full.lifetime.totalGameMs)} · total turn time ${f(full.lifetime.totalTurnMs)}", color = p.muted)
                        Text("Last game: ${full.lastGame.result} · ${f(full.lastGame.durationMs)} · ${full.lastGame.turns} turns", color = p.muted)
                        ToneButton("Share ${row.player.name}'s stats",
                            { onShareStats(state.library.statsShareText(row.player.localId)) }, Modifier.fillMaxWidth())
                    }
                }
            }
        }
    }
}
