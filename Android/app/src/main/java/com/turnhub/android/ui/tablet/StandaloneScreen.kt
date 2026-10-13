package com.turnhub.android.ui.tablet

import androidx.activity.compose.BackHandler
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawingPadding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.FilterChip
import androidx.compose.material3.FilterChipDefaults
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.SuggestionChip
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.unit.dp
import com.turnhub.android.data.TabletState
import com.turnhub.android.domain.TableClock
import com.turnhub.android.domain.TurnTimerStatus
import com.turnhub.android.protocol.TableState
import com.turnhub.android.standalone.LocalPlayer
import com.turnhub.android.standalone.StandaloneGame
import com.turnhub.android.standalone.StandaloneState
import com.turnhub.android.standalone.StandaloneTable
import com.turnhub.android.ui.components.AccentButton
import com.turnhub.android.ui.components.BrassCard
import com.turnhub.android.ui.components.Eyebrow
import com.turnhub.android.ui.components.ToneButton
import com.turnhub.android.ui.components.rememberNowMs
import com.turnhub.android.ui.components.tableBackground
import com.turnhub.android.ui.theme.palette
import kotlinx.coroutines.launch

/**
 * The standalone tablet game: the same table screen as tablet mode, run by the
 * app itself because no Atlas is at the table. Finished games wait on this
 * device until an Atlas imports them.
 */
@Composable
fun StandaloneScreen(
    state: StandaloneState,
    table: StandaloneTable,
    reduceMotion: Boolean,
    onClose: () -> Unit,
    modifier: Modifier = Modifier,
    onHistory: () -> Unit = {},
    atlasEnabled: Boolean = true,
    onAtlasEnabled: (Boolean) -> Unit = {},
) {
    val p = palette
    KeepScreenOn()
    BackHandler(onBack = onClose)
    val scope = rememberCoroutineScope()
    val game = state.game
    Box(modifier.fillMaxSize().tableBackground(p)) {
        if (state.storageProblem != null) {
            Column(Modifier.safeDrawingPadding().padding(24.dp)) {
                Text(state.storageProblem, color = p.text)
                TextButton(onClick = { table.startFresh() }) { Text("Keep old data aside and start fresh") }
                TextButton(onClick = onClose) { Text("Back") }
            }
        } else if (game.state == TableState.LOBBY) {
            Lobby(state, table, onClose, onHistory, atlasEnabled, onAtlasEnabled)
        } else {
            Immersive()
            // The clocks tick on their own from the summary; it is rebuilt only when the game
            // changes or the turn timer's phase (warning, time up) moves on, so the panels
            // aren't redrawn every tick.
            rememberNowMs(ticking = game.state == TableState.RUNNING, intervalMs = 500)
            val phase = game.turnTimer(System.currentTimeMillis()).phase
            val summary = remember(game, phase, state.library.players) { game.toSummary(System.currentTimeMillis(), TableClock.nowMs(),
                state.library.players.associate { it.localId to it.personalization }) }
            TabletTable(
                summary = summary,
                tablet = TabletState(),
                controls = { block -> scope.launch { table.block() } },
                onDismissMessage = {},
                reduceMotion = reduceMotion || state.library.players.any { it.personalization.reduceMotion },
                onClose = onClose,
                standalone = true,
            )
        }
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun Lobby(
    state: StandaloneState,
    table: StandaloneTable,
    onClose: () -> Unit,
    onHistory: () -> Unit,
    atlasEnabled: Boolean,
    onAtlasEnabled: (Boolean) -> Unit,
) {
    val p = palette
    val game = state.game
    Box(Modifier.fillMaxSize().safeDrawingPadding(), contentAlignment = Alignment.TopCenter) {
        Column(
            Modifier.widthIn(max = 640.dp).fillMaxWidth().verticalScroll(rememberScrollState()).padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(14.dp),
        ) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text("Full-screen table", color = p.text, style = MaterialTheme.typography.headlineSmall,
                    modifier = Modifier.weight(1f).semantics { heading() })
                if (atlasEnabled) TextButton(onClick = onClose) { Text("Atlas", color = p.muted) }
            }
            Text(
                "This phone or tablet becomes the table: lay it in the middle and everyone plays on one screen. " +
                    "It keeps life, Commander damage, counters, turns, the turn timer and the winner. " +
                    "Your game is saved when you close the app.",
                color = p.muted,
            )

            BrassCard {
                Eyebrow("At the table")
                if (game.players.isEmpty()) Text("Add everyone who is playing.", color = p.muted)
                if (game.players.size > 1) {
                    // How the game will look: each panel where it lands, numbered in turn order.
                    SeatMap(game.players.map { it.name }, table::swapPlayers)
                    Text("This is how the table will look. Drag a player onto another seat to swap them, or tap two seats. " +
                        "Turns go clockwise in number order.", color = p.faint, style = MaterialTheme.typography.bodySmall)
                }
                game.players.forEachIndexed { index, player -> PlayerRow(index, player, game.players.size, table) }
                var name by rememberSaveable { mutableStateOf("") }
                val full = game.players.size >= StandaloneGame.MAX_PLAYERS
                val add = {
                    if (name.isNotBlank()) {
                        table.addPlayer(name)
                        name = ""
                    }
                }
                Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    OutlinedTextField(
                        value = name,
                        onValueChange = { name = it.take(StandaloneGame.MAX_NAME) },
                        label = { Text("Player's name") },
                        singleLine = true,
                        keyboardOptions = KeyboardOptions(imeAction = ImeAction.Done),
                        keyboardActions = KeyboardActions(onDone = { add() }),
                        modifier = Modifier.weight(1f),
                    )
                    ToneButton("Add", add, enabled = name.isNotBlank() && !full)
                }
                val seated = game.players.mapNotNull { it.localId }.toSet()
                val free = state.library.players.filter { it.localId !in seated }
                if (free.isNotEmpty()) {
                    Text("Saved on this device", color = p.muted, style = MaterialTheme.typography.labelLarge)
                    FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
                        free.forEach { player ->
                            SuggestionChip(
                                onClick = { table.selectPlayer(player.localId) },
                                label = { Text(player.name) },
                                enabled = !full,
                            )
                        }
                    }
                }
                Text("Add creates a new player. Pick a saved player to keep their results together; equal names stay separate.",
                    color = p.faint, style = MaterialTheme.typography.bodySmall)
            }

            BrassCard {
                Eyebrow("Game")
                FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    FORMATS.forEach { (profile, label, _) ->
                        FilterChip(
                            selected = game.profile == profile,
                            onClick = { table.setFormat(profile) },
                            label = { Text(label) },
                            colors = FilterChipDefaults.filterChipColors(selectedContainerColor = p.accentSoft),
                        )
                    }
                }
                var life by rememberSaveable(game.startingLife) { mutableStateOf("${game.startingLife}") }
                Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    OutlinedTextField(
                        value = life,
                        onValueChange = { life = it.filter(Char::isDigit).take(5) },
                        label = { Text("Starting life") },
                        singleLine = true,
                        keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number),
                        modifier = Modifier.weight(1f),
                    )
                    ToneButton(
                        "Set",
                        { life.toIntOrNull()?.let(table::setStartingLife) },
                        enabled = (life.toIntOrNull() ?: 0) > 0 && life != "${game.startingLife}",
                    )
                }
                Text("Turn timer", color = p.muted, style = MaterialTheme.typography.labelLarge)
                FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    StandaloneGame.TURN_TIMER_PRESETS_MS.forEach { ms ->
                        FilterChip(
                            selected = game.turnTimerMs == ms,
                            onClick = { table.setTurnTimer(ms) },
                            label = { Text(TurnTimerStatus.settingLabel(ms)) },
                            colors = FilterChipDefaults.filterChipColors(selectedContainerColor = p.accentSoft),
                        )
                    }
                }
                Text("Running out never passes the turn: the clock counts the overtime until the player passes.",
                    color = p.faint, style = MaterialTheme.typography.bodySmall)
            }

            AccentButton(
                if (game.players.size < 2) "Add two players to start" else "Start full-screen game",
                table::start,
                Modifier.fillMaxWidth().heightIn(min = 64.dp),
                enabled = game.players.size >= 2,
            )

            ToneButton("Automatic elimination at zero life: " + if (game.autoEliminateAtZeroLife) "On" else "Off",
                { table.setZeroLifeElimination(!game.autoEliminateAtZeroLife) }, Modifier.fillMaxWidth())
            ToneButton("Players and history", onHistory, Modifier.fillMaxWidth())

            // Quiet on purpose: a phone-only player never needs Atlas, so it is one small switch.
            TextButton(onClick = { onAtlasEnabled(!atlasEnabled) }, modifier = Modifier.fillMaxWidth()) {
                Text(
                    if (atlasEnabled) "TurnHub Atlas table: shown. Turn off if you play on this device only"
                    else "TurnHub Atlas table: hidden. Turn on to connect Sigils and a phone for every player",
                    color = p.faint, style = MaterialTheme.typography.bodySmall,
                )
            }
        }
    }
}

@Composable
private fun PlayerRow(index: Int, player: LocalPlayer, count: Int, table: StandaloneTable) {
    val p = palette
    Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(10.dp)) {
        Box(Modifier.size(30.dp).background(p.avatarColor(index + 1), CircleShape), contentAlignment = Alignment.Center) {
            Text("${index + 1}", color = p.avatarText, style = MaterialTheme.typography.labelLarge)
        }
        Column(Modifier.weight(1f)) {
            Text(player.name, color = p.text, style = MaterialTheme.typography.bodyLarge)
        }
        IconButton(onClick = { table.removePlayer(index) }) {
            Text("×", color = p.muted, style = MaterialTheme.typography.titleLarge,
                modifier = Modifier.semantics { contentDescription = "Remove ${player.name}" })
        }
    }
}

