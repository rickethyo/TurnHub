package com.turnhub.android.ui.tablet

import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.*
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.ui.Modifier
import androidx.compose.ui.semantics.*
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import com.turnhub.android.domain.TablePlayer
import com.turnhub.android.domain.TableSummary
import com.turnhub.android.protocol.DoomAction
import com.turnhub.android.protocol.TableState
import com.turnhub.android.ui.components.AccentButton
import com.turnhub.android.ui.components.ToneButton
import com.turnhub.android.ui.theme.palette

/** A modal window fills the available display, including on large tablets. */
@Composable
private fun FullScreenGamePrompt(title: String, onDismiss: () -> Unit, content: @Composable ColumnScope.() -> Unit) {
    val p = palette
    Dialog(onDismissRequest = onDismiss,
        properties = DialogProperties(usePlatformDefaultWidth = false, decorFitsSystemWindows = false)) {
        Surface(Modifier.fillMaxSize(), color = p.bg) {
            Column(Modifier.fillMaxSize().safeDrawingPadding().verticalScroll(rememberScrollState()).padding(24.dp),
                verticalArrangement = Arrangement.spacedBy(16.dp)) {
                Text(title, color = p.text, style = MaterialTheme.typography.headlineLarge,
                    modifier = Modifier.semantics { heading(); liveRegion = LiveRegionMode.Assertive })
                content()
            }
        }
    }
}

/** source: null = unknown, 0 = Self, positive = explicit opponent player number. */
@Composable
internal fun EliminationSourceDialog(summary: TableSummary, victim: TablePlayer, pending: Boolean,
    enabled: Boolean, onConfirm: (Int?) -> Unit, onCancel: () -> Unit = {}) {
    val p = palette
    var choice by rememberSaveable(summary.bootId, summary.game, victim.playerNumber, pending) { mutableStateOf<Int?>(null) }
    FullScreenGamePrompt("Eliminated by?", onCancel) {
        Text("${victim.playerNumber}. ${victim.label}", color = p.text, style = MaterialTheme.typography.headlineSmall)
        Text("Choose the actual player, including off-turn effects. Self and Unknown award no opponent credit.", color = p.muted)
        val choices = listOf(0 to if (victim.team == null) "Self" else "Self / own team", -1 to "Unknown / no player credited") +
            summary.players.filter { it.playerNumber != victim.playerNumber && (victim.team == null || victim.team != it.team) }
                .map { it.playerNumber to "${it.playerNumber}. ${it.label}${if (it.eliminated) " (out)" else ""}" }
        choices.forEach { (value, label) ->
            ToneButton((if (choice == value) "✓ " else "") + label, { choice = value },
                Modifier.fillMaxWidth().semantics { selected = choice == value }, enabled = enabled)
        }
        AccentButton(if (pending) "Record elimination source" else "Confirm elimination", {
            choice?.let { onConfirm(it.takeIf { n -> n >= 0 }) }
        }, Modifier.fillMaxWidth(), enabled = enabled && choice != null)
        ToneButton(if (pending) "Review game" else "Cancel", onCancel, Modifier.fillMaxWidth())
    }
}

@Composable
internal fun DoomBagDialog(summary: TableSummary, enabled: Boolean, onAction: (DoomAction, Int, Int) -> Unit,
    onResume: () -> Unit, onDismiss: () -> Unit) {
    val bag = summary.doomBag ?: return
    val p = palette
    FullScreenGamePrompt(if (bag.pending) "Bag check" else "Bag of Doom", onDismiss) {
        if (bag.pending) Text("Play is paused. Review the bag before the next turn.", color = p.warn,
            style = MaterialTheme.typography.titleLarge)
        else if (bag.lastCheck > 0) Text(if (bag.lastDraw == 0) "Everyone is safe" else
            "${summary.players.firstOrNull { it.playerNumber == bag.lastDraw }?.label ?: "Player ${bag.lastDraw}"} is out",
            color = p.text, style = MaterialTheme.typography.headlineMedium)
        DoomBagPanel(summary, enabled, onAction)
        if (!bag.pending && summary.state == TableState.PAUSED)
            AccentButton("Continue game", onResume, Modifier.fillMaxWidth(), enabled = enabled)
        ToneButton(if (bag.pending) "Review life totals" else "Close", onDismiss, Modifier.fillMaxWidth())
    }
}
