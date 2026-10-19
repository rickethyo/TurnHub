package com.turnhub.android.ui.tablet

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.semantics.LiveRegionMode
import androidx.compose.ui.semantics.liveRegion
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.dp
import com.turnhub.android.domain.TableSummary
import com.turnhub.android.protocol.DoomAction
import com.turnhub.android.protocol.LifeRequestState
import com.turnhub.android.protocol.TableState
import com.turnhub.android.ui.components.AccentButton
import com.turnhub.android.ui.components.DisclosureCard
import com.turnhub.android.ui.components.ToneButton
import com.turnhub.android.ui.home.ChoiceDropdown
import com.turnhub.android.ui.theme.palette

/** Presentation only: every correction, credit and draw goes to the game authority. */
@Composable
internal fun DoomBagPanel(summary: TableSummary, enabled: Boolean,
    onAction: (DoomAction, Int, Int) -> Unit) {
    val bag = summary.doomBag ?: return
    val p = palette
    val living = summary.players.filter { !it.eliminated }
    val active = living.size > 4 && summary.state in listOf(TableState.RUNNING, TableState.PAUSED)
    val pendingLife = summary.players.any { it.lifeRequest?.state == LifeRequestState.PENDING }
    val canEdit = enabled && active && !bag.sealed
    fun name(n: Int) = summary.players.firstOrNull { it.playerNumber == n }?.let { "${it.playerNumber}. ${it.label}" } ?: "Player $n"
    Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
        Text(when {
            !active -> "The bag is off. Continue with normal Commander."
            bag.pending -> "Bag check due before the next turn"
            bag.warmup > 0 -> "First check in ${bag.warmup} turn cycle${if (bag.warmup == 1) "" else "s"}"
            else -> "Next check after ${name(bag.boundary)}’s turn"
        }, color = p.text, style = MaterialTheme.typography.titleMedium,
            modifier = Modifier.semantics { liveRegion = LiveRegionMode.Polite })
        if (bag.lastCheck > 0) Text(if (bag.lastDraw == 0) "Last check: everyone was safe. Nobody was eliminated."
            else "Drawn: ${name(bag.lastDraw)}. Eliminated by the bag; no player receives credit.",
            color = p.text, modifier = Modifier.semantics { liveRegion = LiveRegionMode.Polite })
        if (active) {
            val count = living.count { bag.contains(it.playerNumber) }
            Text("$count of ${living.size} players in the bag", color = p.muted)
            Text("Another player losing life on your turn makes you safe, including paying life. Use these controls to correct missed or mistaken entries.", color = p.muted)
            living.forEach { player ->
                val risk = bag.contains(player.playerNumber)
                Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    Text("${player.playerNumber}. ${player.label} · ${if (risk) "In bag" else "Safe"}", color = p.text,
                        modifier = Modifier.weight(1f).padding(top = 12.dp))
                    ToneButton(if (risk) "Mark safe" else "Put in bag",
                        { onAction(DoomAction.ELIGIBILITY, player.playerNumber, if (risk) 0 else 1) }, enabled = canEdit)
                }
            }
            if (bag.pending) {
                if (pendingLife) Text("Resolve pending life requests before drawing.", color = p.warn)
                AccentButton(if (bag.sealed) "Retry the same draw" else if (count == 0) "Finish check · everyone safe" else "Draw from bag",
                    { onAction(DoomAction.DRAW, 0, 0) }, Modifier.fillMaxWidth(), enabled = enabled && !pendingLife)
            }
        }
        if (summary.state in listOf(TableState.RUNNING, TableState.PAUSED)) {
            val uncredited = summary.players.filter { it.eliminated && !it.conceded && !it.selfEliminated && !it.eliminationPending && it.eliminatedBy == null &&
                bag.drawn and (1 shl (it.playerNumber - 1)) == 0 }
            if (uncredited.isNotEmpty()) DisclosureCard("Credit an earlier elimination") {
                Text("Use this after an automatic removal. Credit resets the bag and its boundary. Each elimination can be credited once.", color = p.muted)
                uncredited.forEach { victim ->
                    var source by rememberSaveable(summary.bootId, victim.participantId) { mutableIntStateOf(0) }
                    ChoiceDropdown(label = "Who eliminated ${name(victim.playerNumber)}?",
                        options = listOf(0 to "Choose a player") + summary.players.filter { it.playerNumber != victim.playerNumber }
                            .map { it.playerNumber to name(it.playerNumber) }, selected = source, onSelect = { source = it },
                        enabled = enabled && !bag.sealed)
                    AccentButton("Credit elimination", { onAction(DoomAction.CREDIT, victim.playerNumber, source) },
                        Modifier.fillMaxWidth(), enabled = enabled && source != 0 && !bag.sealed)
                }
            }
        }
    }
}
