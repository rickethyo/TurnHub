package com.turnhub.android.ui.home

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.ModalBottomSheet
import androidx.compose.material3.Text
import androidx.compose.material3.rememberModalBottomSheetState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.dp
import com.turnhub.android.data.ProfileStatistics
import com.turnhub.android.data.ProfileStatistics.Companion.duration
import com.turnhub.android.data.StatisticsLoad
import com.turnhub.android.ui.components.AccentButton
import com.turnhub.android.ui.components.BrassCard
import com.turnhub.android.ui.components.Eyebrow
import com.turnhub.android.ui.components.Gauge
import com.turnhub.android.ui.components.Metric
import com.turnhub.android.ui.components.StatusRow
import com.turnhub.android.ui.components.Tone
import com.turnhub.android.ui.components.ToneButton
import com.turnhub.android.ui.theme.DesignTokens
import com.turnhub.android.ui.theme.palette

/*
 * Statistics, as the portal's Statistics page: a summary card on My Account
 * and a sheet with every number Atlas keeps for the signed-in profile. Atlas
 * owns the numbers; the app only formats them.
 */

/** The My Account card: win-rate ring, games played and won, and the way into the full sheet. */
@Composable
internal fun StatsCard(load: StatisticsLoad?, reduceMotion: Boolean, actions: AccountActions) {
    val p = palette
    LaunchedEffect(Unit) { actions.onLoadStats() }
    var open by rememberSaveable { mutableStateOf(false) }
    val stats = load?.stats
    BrassCard {
        Eyebrow("My statistics")
        when {
            stats != null -> {
                Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(14.dp)) {
                    Gauge(
                        value = "${Math.round(stats.winFraction * 100)}%",
                        caption = "Win rate",
                        fraction = stats.winFraction,
                        size = 120.dp,
                        reduceMotion = reduceMotion,
                    )
                    Column(Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                        Metric("Games played", stats.lifetime.gamesPlayed.toString(), Modifier.fillMaxWidth())
                        Metric("Games won", stats.lifetime.gamesWon.toString(), Modifier.fillMaxWidth())
                    }
                }
                ToneButton("All statistics", { open = true }, Modifier.fillMaxWidth(), tone = Tone.INFO)
            }
            load?.error != null -> Text(load.error, color = p.bad, style = MaterialTheme.typography.bodyMedium)
            else -> Text("Reading your statistics from Atlas…", color = p.muted, style = MaterialTheme.typography.bodySmall)
        }
        Text(
            "Statistics are stored on this Atlas and stay with your profile, not with a physical Sigil.",
            color = p.faint,
            style = MaterialTheme.typography.bodySmall,
        )
    }
    if (open && stats != null) {
        StatsSheet(stats, load.error, actions, onDismiss = { open = false })
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun StatsSheet(stats: ProfileStatistics, error: String?, actions: AccountActions, onDismiss: () -> Unit) {
    val p = palette
    // Re-read on open, so a game finished since the card loaded shows up.
    LaunchedEffect(Unit) { actions.onLoadStats() }
    val l = stats.lifetime
    val g = stats.lastGame
    ModalBottomSheet(
        onDismissRequest = onDismiss,
        sheetState = rememberModalBottomSheetState(skipPartiallyExpanded = true),
        containerColor = p.bg,
    ) {
        Column(
            Modifier
                .fillMaxWidth()
                .verticalScroll(rememberScrollState())
                .padding(horizontal = DesignTokens.Layout.gutter)
                .navigationBarsPadding()
                .padding(bottom = DesignTokens.Space.s5),
            verticalArrangement = Arrangement.spacedBy(DesignTokens.Space.s5),
        ) {
            Column {
                Text(
                    stats.name,
                    color = p.text,
                    style = MaterialTheme.typography.titleLarge,
                    modifier = Modifier.semantics { heading() },
                )
                Text("Profile ${stats.profileId}", color = p.faint, style = MaterialTheme.typography.bodySmall)
            }
            error?.let { Text(it, color = p.bad, style = MaterialTheme.typography.bodyMedium) }
            if (!stats.detailed) {
                Text(
                    "Detailed statistics (turn times and last-game details) are kept on the Atlas microSD card. No card " +
                        "is inserted, so only games played and won, and the last result, are being recorded.",
                    color = p.warn,
                    style = MaterialTheme.typography.bodySmall,
                )
            }
            BrassCard {
                Eyebrow("Lifetime")
                StatusRow("Games played", l.gamesPlayed.toString())
                StatusRow("Games won", l.gamesWon.toString())
                StatusRow("Win rate", stats.winRate)
                StatusRow("Started first", l.gamesStarted.toString())
                StatusRow("Eliminated", l.gamesEliminated.toString())
                StatusRow("Completed turns", l.turnsCompleted.toString())
            }
            BrassCard {
                Eyebrow("Turn records")
                StatusRow("Total completed-turn time", duration(l.totalTurnMs))
                StatusRow("Average completed turn", duration(l.averageTurnMs, l.turnsCompleted > 0))
                StatusRow("Fastest completed turn", duration(l.fastestTurnMs, l.fastestTurnMs > 0))
                StatusRow("Longest completed turn", duration(l.longestTurnMs, l.longestTurnMs > 0))
            }
            BrassCard {
                Eyebrow("Game time")
                StatusRow("Total game time", duration(l.totalGameMs))
                StatusRow("Average game time", duration(l.averageGameMs, l.gamesPlayed > 0))
            }
            BrassCard {
                Eyebrow("Most recent game")
                StatusRow("Result", g.result)
                StatusRow("Game duration", duration(g.durationMs, l.gamesPlayed > 0))
                StatusRow("Completed turns", g.turns.toString())
                StatusRow("Average turn", duration(g.averageTurnMs, g.turns > 0))
                StatusRow("Fastest turn", duration(g.fastestTurnMs, g.fastestTurnMs > 0))
                StatusRow("Longest turn", duration(g.longestTurnMs, g.longestTurnMs > 0))
            }
            BrassCard {
                Eyebrow("Private moderation history")
                Text(
                    "Only you can see this, and only after signing in with your PIN or password. It is never included " +
                        "in shared statistics or shown to Game Masters.",
                    color = p.muted,
                    style = MaterialTheme.typography.bodySmall,
                )
                val m = stats.moderation
                if (m.visible) {
                    StatusRow("Connection resets by a Game Master", m.connectionResets.toString())
                    StatusRow("Removals from a game by a Game Master", m.gameRemovals.toString())
                } else {
                    Text(
                        m.reason ?: "Sign in with your PIN to see your private moderation history.",
                        color = p.muted,
                        style = MaterialTheme.typography.bodyMedium,
                    )
                }
            }
            AccentButton("Share my stats", actions.onShareStats, Modifier.fillMaxWidth())
        }
    }
}
