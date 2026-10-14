package com.turnhub.android.ui.tablet

import androidx.activity.compose.BackHandler
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawingPadding
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.setValue
import androidx.compose.runtime.remember
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import com.turnhub.android.data.TabletState
import com.turnhub.android.domain.TableClock
import com.turnhub.android.protocol.TableState
import com.turnhub.android.standalone.StandaloneGame
import com.turnhub.android.standalone.StandaloneState
import com.turnhub.android.standalone.StandaloneTable
import com.turnhub.android.standalone.shareSummary
import com.turnhub.android.ui.components.ToneButton
import com.turnhub.android.ui.components.displayName
import com.turnhub.android.ui.components.shareGameResult
import androidx.compose.ui.graphics.toArgb
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
    onStats: () -> Unit = {},
    atlasEnabled: Boolean = true,
    onAtlasEnabled: (Boolean) -> Unit = {},
) {
    val p = palette
    KeepScreenOn()
    BackHandler(onBack = onClose)
    val scope = rememberCoroutineScope()
    val context = androidx.compose.ui.platform.LocalContext.current
    val game = state.game
    val soundSummary = remember(game) { game.toSummary(0, 0) }
    com.turnhub.android.ui.components.GameSoundEffect(soundSummary, available = state.storageProblem == null)
    Box(modifier.fillMaxSize().tableBackground(p)) {
        if (state.storageProblem != null) {
            Column(Modifier.safeDrawingPadding().padding(24.dp)) {
                Text(state.storageProblem, color = p.text)
                TextButton(onClick = { table.startFresh() }) { Text("Keep old data aside and start fresh") }
                TextButton(onClick = onClose) { Text("Back") }
            }
        } else if (game.state == TableState.LOBBY) {
            Lobby(state, table, onClose, onHistory, onStats, atlasEnabled, onAtlasEnabled)
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
                onShareResult = game.record()?.let { record ->
                    val text = record.shareSummary(record.profile.displayName())
                    val share: (android.graphics.Bitmap) -> Unit = { picture ->
                        scope.launch { shareGameResult(context, picture, text, p.bg.toArgb(), p.text.toArgb(), p.muted.toArgb()) }
                    }
                    share
                },
                shortGameUnsaved = table.unsavedShortGame() != null,
                onCountGame = { table.saveShortGame() },
            )
        }
    }
}

@Composable
private fun Lobby(
    state: StandaloneState,
    table: StandaloneTable,
    onClose: () -> Unit,
    onHistory: () -> Unit,
    onStats: () -> Unit,
    atlasEnabled: Boolean,
    onAtlasEnabled: (Boolean) -> Unit,
) {
    val game = state.game
    val summary = remember(game, state.library.players) {
        game.toSummary(0, 0, state.library.players.associate { it.localId to it.personalization })
    }
    var editingLocalId by rememberSaveable { mutableStateOf<String?>(null) }
    state.library.players.firstOrNull { it.localId == editingLocalId }?.let { account ->
        LocalTileBackgroundDialog(account, table) { editingLocalId = null }
    }

    val seated = game.players.mapNotNull { it.localId }.toSet()
    TableLobby(
        summary = summary,
        actions = TableLobbyActions(
            add = table::addPlayer,
            select = { table.selectPlayer(it.id) },
            remove = { table.removePlayer(it.playerNumber - 1) },
            canRemove = { true },
            clear = table::clearPlayers,
            swap = table::swapPlayers,
            settings = table::configure,
            timer = table::setTurnTimer,
            zeroLife = table::setZeroLifeElimination,
            houseRules = table::setHouseRules,
            chooseStarter = { table.chooseStarter(it.playerNumber - 1) },
            start = table::start,
            background = { editingLocalId = game.players.getOrNull(it.playerNumber - 1)?.localId },
            canEditBackground = { player -> game.players.getOrNull(player.playerNumber - 1)?.localId in state.library.players.map { it.localId } },
        ),
        savedPlayers = state.library.players.filter { it.localId !in seated }.map { LobbySavedPlayer(it.localId, it.name) },
        enabled = true,
        settingsEnabled = true,
        canAdd = game.players.size < StandaloneGame.MAX_PLAYERS,
        identityHint = "Add creates a new player. Pick a saved player to keep their results together; equal names stay separate.",
        savedTitle = "Saved on this device",
        starterHint = "Nobody chosen: a random player goes first. Tap First? to choose.",
        introduction = "This phone or tablet becomes the table: lay it in the middle and everyone plays on one screen. " +
            "It keeps life, Commander damage, counters, turns, the turn timer and the winner. " +
            "Your game is saved when you close the app.",
        headerActions = {
            val context = androidx.compose.ui.platform.LocalContext.current
            TextButton(onClick = { com.turnhub.android.ui.components.openBugReport(context) }) {
                Text("Report a bug", color = palette.muted)
            }
            if (atlasEnabled) TextButton(onClick = onClose) { Text("Atlas", color = palette.muted) }
        },
        footer = {
            androidx.compose.foundation.layout.Row(Modifier.fillMaxWidth(), horizontalArrangement = androidx.compose.foundation.layout.Arrangement.spacedBy(10.dp)) {
                ToneButton("Stats", onStats, Modifier.weight(1f))
                ToneButton("Players and history", onHistory, Modifier.weight(1f))
            }
            TextButton(onClick = { onAtlasEnabled(!atlasEnabled) }, modifier = Modifier.fillMaxWidth()) {
                Text(if (atlasEnabled) "TurnHub Atlas table: shown. Turn off if you play on this device only"
                    else "TurnHub Atlas table: hidden. Turn on to connect Sigils and a phone for every player",
                    color = palette.faint, style = MaterialTheme.typography.bodySmall)
            }
        },
    )
}
