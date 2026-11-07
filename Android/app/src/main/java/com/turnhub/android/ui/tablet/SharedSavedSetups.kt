package com.turnhub.android.ui.tablet

import androidx.compose.runtime.Composable
import com.turnhub.android.lan.*

@Composable
internal fun SharedSavedSetups(state: LanUiState, model: LanViewModel) {
    val game = state.remote ?: return
    val enabled = state.online && !state.busy && !state.uncertain && (state.role in setOf("player", "table") || state.master)
    SavedSetupActions(state.setupNames, enabled, game.players.isNotEmpty(), "Stored on the host device.", summaries = state.setupDetails) { op, name ->
        model.setup(game, when (op) { "load" -> "load-setup"; "delete" -> "delete-setup"; else -> "save-setup" },
            "name" to name, "replace" to (op == "replace"))
    }
}
