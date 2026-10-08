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
import com.turnhub.android.protocol.TableState
import com.turnhub.android.standalone.LocalPlayer
import com.turnhub.android.standalone.StandaloneGame
import com.turnhub.android.standalone.StandaloneState
import com.turnhub.android.standalone.StandaloneTable
import com.turnhub.android.ui.components.AccentButton
import com.turnhub.android.ui.components.BrassCard
import com.turnhub.android.ui.components.Eyebrow
import com.turnhub.android.ui.components.ToneButton
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
            Lobby(state, table, onClose, onHistory)
        } else {
            Immersive()
            TabletTable(
                summary = game.toSummary(System.currentTimeMillis(), TableClock.nowMs()),
                tablet = TabletState(),
                controls = { block -> scope.launch { table.block() } },
                onDismissMessage = {},
                reduceMotion = reduceMotion,
                onClose = onClose,
                standalone = true,
            )
        }
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun Lobby(state: StandaloneState, table: StandaloneTable, onClose: () -> Unit, onHistory: () -> Unit) {
    val p = palette
    val game = state.game
    Box(Modifier.fillMaxSize().safeDrawingPadding(), contentAlignment = Alignment.TopCenter) {
        Column(
            Modifier.widthIn(max = 640.dp).fillMaxWidth().verticalScroll(rememberScrollState()).padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(14.dp),
        ) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text("Play on this device", color = p.text, style = MaterialTheme.typography.headlineSmall,
                    modifier = Modifier.weight(1f).semantics { heading() })
                TextButton(onClick = onClose) { Text("Back", color = p.muted) }
            }
            Text(
                "This device keeps the game: life, Commander damage, turns and the winner. " +
                    "Add names below and start playing. Your game is saved when you close the app.",
                color = p.muted,
            )

            BrassCard {
                Eyebrow("At the table")
                if (game.players.isEmpty()) Text("Add everyone who is playing.", color = p.muted)
                game.players.forEachIndexed { index, player -> PlayerRow(index, player, game.players.size, table) }
                if (game.players.size > 1) {
                    Text("Arrows set the turn order; panels sit clockwise in this order.", color = p.faint,
                        style = MaterialTheme.typography.bodySmall)
                }
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
            }

            ToneButton("Players and history", onHistory, Modifier.fillMaxWidth())

            AccentButton(
                if (game.players.size < 2) "Add two players to start" else "Start the game",
                table::start,
                Modifier.fillMaxWidth(),
                enabled = game.players.size >= 2,
            )
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
        MoveButton("↑", "Move ${player.name} earlier in turn order", index > 0) { table.movePlayer(index, -1) }
        MoveButton("↓", "Move ${player.name} later in turn order", index < count - 1) { table.movePlayer(index, 1) }
        IconButton(onClick = { table.removePlayer(index) }) {
            Text("×", color = p.muted, style = MaterialTheme.typography.titleLarge,
                modifier = Modifier.semantics { contentDescription = "Remove ${player.name}" })
        }
    }
}

