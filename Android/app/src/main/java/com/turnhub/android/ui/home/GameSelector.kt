package com.turnhub.android.ui.home

import androidx.compose.runtime.Composable
import com.turnhub.android.domain.TableSummary
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.heightIn
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.getValue
import androidx.compose.runtime.setValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.ui.Modifier
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.stateDescription
import androidx.compose.ui.unit.dp

/** Atlas owns the venue list and validates personal/shared-tablet moves. */
@Composable
fun GameSelector(summary: TableSummary, onSelect: (Int) -> Unit, enabled: Boolean = true, compact: Boolean = false) {
    if (summary.games.size < 2) return
    if (compact) {
        var expanded by rememberSaveable { mutableStateOf(false) }
        Box {
            TextButton(onClick = { expanded = true }, enabled = enabled,
                modifier = Modifier.heightIn(min = 48.dp).semantics { stateDescription = if (expanded) "Expanded" else "Collapsed" }) {
                Text("Atlas · Game ${summary.game} ▾")
            }
            DropdownMenu(expanded = expanded, onDismissRequest = { expanded = false }) {
                summary.games.forEach { game ->
                    DropdownMenuItem(text = {
                        Text("${if (game.game == summary.game) "✓ " else ""}Game ${game.game} · ${game.state.name.lowercase().replace('_', ' ')} · ${game.players} players")
                    }, onClick = { expanded = false; if (game.game != summary.game) onSelect(game.game) }, enabled = enabled)
                }
            }
        }
        return
    }
    ChoiceDropdown(
        label = "Game",
        options = summary.games.map { it.game to "Game ${it.game} · ${it.state.name.lowercase().replace('_', ' ')} · ${it.players} players" },
        selected = summary.game, onSelect = onSelect, enabled = enabled,
    )
}
