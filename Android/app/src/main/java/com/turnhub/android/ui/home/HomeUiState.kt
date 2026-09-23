package com.turnhub.android.ui.home

import com.turnhub.android.data.AtlasRepository
import com.turnhub.android.protocol.AtlasConnectionState
import com.turnhub.android.protocol.Sigil
import com.turnhub.android.protocol.TableSummary

/**
 * Everything the Home screen needs to render, derived from [AtlasRepository]
 * flows by [HomeViewModel]. Keeping this as one immutable data class (rather
 * than the Composable reading three separate flows itself) keeps [HomeScreen]
 * a pure function of state.
 */
data class HomeUiState(
    val connectionState: AtlasConnectionState = AtlasConnectionState.DISCONNECTED,
    val tableSummary: TableSummary? = null,
    val sigils: List<Sigil> = emptyList(),
)
