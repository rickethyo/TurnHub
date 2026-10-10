package com.turnhub.android.ui.home

import androidx.compose.runtime.Composable
import com.turnhub.android.domain.TableSummary

/** Atlas owns the venue list and validates personal/shared-tablet moves. */
@Composable
fun GameSelector(summary: TableSummary, onSelect: (Int) -> Unit, enabled: Boolean = true) {
    if (summary.games.size < 2) return
    ChoiceDropdown(
        label = "Game on this Atlas",
        options = summary.games.map { it.game to "Game ${it.game} · ${it.state.name.lowercase().replace('_', ' ')} · ${it.players} players" },
        selected = summary.game, onSelect = onSelect, enabled = enabled,
    )
}
