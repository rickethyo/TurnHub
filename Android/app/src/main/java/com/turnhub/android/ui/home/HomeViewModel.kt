package com.turnhub.android.ui.home

import androidx.lifecycle.ViewModel
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewModelScope
import androidx.lifecycle.viewmodel.initializer
import androidx.lifecycle.viewmodel.viewModelFactory
import com.turnhub.android.data.AtlasRepository
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.stateIn
import kotlinx.coroutines.launch

/**
 * Combines [AtlasRepository] state into [HomeUiState] and exposes the two user
 * actions the Home screen offers this milestone: connect/disconnect.
 *
 * This class knows nothing about HTTP, ESP-NOW, or Compose; it depends only on
 * the [AtlasRepository] interface, so swapping [com.turnhub.android.data.MockAtlasRepository]
 * for a real implementation later should not require changing this file.
 */
class HomeViewModel(
    private val repository: AtlasRepository,
) : ViewModel() {

    val uiState: StateFlow<HomeUiState> = combine(
        repository.connectionState,
        repository.tableSummary,
        repository.sigils,
    ) { connectionState, tableSummary, sigils ->
        HomeUiState(
            connectionState = connectionState,
            tableSummary = tableSummary,
            sigils = sigils,
        )
    }.stateIn(
        scope = viewModelScope,
        started = SharingStarted.WhileSubscribed(stopTimeoutMillis = 5_000),
        initialValue = HomeUiState(),
    )

    fun onConnectClicked() {
        viewModelScope.launch { repository.connect() }
    }

    fun onDisconnectClicked() {
        viewModelScope.launch { repository.disconnect() }
    }

    companion object {
        /**
         * Minimal manual-DI factory: no framework is introduced for one ViewModel
         * with one constructor argument. Revisit if/when the dependency graph
         * actually grows past what this can comfortably express.
         */
        fun factory(repository: AtlasRepository): ViewModelProvider.Factory = viewModelFactory {
            initializer { HomeViewModel(repository) }
        }
    }
}
