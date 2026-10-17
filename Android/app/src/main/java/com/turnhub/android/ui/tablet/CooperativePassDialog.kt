package com.turnhub.android.ui.tablet

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import com.turnhub.android.domain.CooperativeTurn
import com.turnhub.android.domain.TableSummary
import com.turnhub.android.ui.components.ToneButton

@Composable
internal fun CooperativePassDialog(summary: TableSummary, turn: CooperativeTurn, onDismiss: () -> Unit, onChoose: (Int) -> Unit) {
    val nextRound = turn.playerNumber in summary.passPlayerNumbers
    AlertDialog(onDismissRequest = onDismiss, title = { Text("Pass to?") }, text = {
        Column(Modifier.fillMaxWidth().heightIn(max = 400.dp).verticalScroll(rememberScrollState()),
            verticalArrangement = Arrangement.spacedBy(10.dp)) {
            Text(if (nextRound) "Round ${turn.round + 1} starts next. Who goes first?"
                else "Who takes the next turn in round ${turn.round}?")
            summary.passPlayerNumbers.forEach { number ->
                val player = summary.players.firstOrNull { it.playerNumber == number } ?: return@forEach
                ToneButton(listOfNotNull(player.label, player.investigator).joinToString("\n"),
                    { onChoose(number) }, Modifier.fillMaxWidth())
            }
        }
    }, confirmButton = {}, dismissButton = { TextButton(onClick = onDismiss) { Text("Cancel") } })
}
