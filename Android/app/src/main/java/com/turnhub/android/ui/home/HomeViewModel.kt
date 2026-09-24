package com.turnhub.android.ui.home

import androidx.lifecycle.ViewModel
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewModelScope
import androidx.lifecycle.viewmodel.initializer
import androidx.lifecycle.viewmodel.viewModelFactory
import com.turnhub.android.data.AtlasEndpoint
import com.turnhub.android.data.AtlasException
import com.turnhub.android.data.AtlasFailure
import com.turnhub.android.data.AtlasRepository
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.stateIn
import kotlinx.coroutines.launch

/**
 * Combines [AtlasRepository] state and the user's endpoint input into
 * [HomeUiState], and exposes the Home screen's actions.
 *
 * The repository is built from [repositoryFactory] with this ViewModel's scope,
 * so its polling lives exactly as long as the screen's ViewModel (surviving
 * rotation, stopping when the screen is finished). This class knows nothing
 * about HTTP; it depends only on the [AtlasRepository] interface.
 */
class HomeViewModel(
    repositoryFactory: (CoroutineScope) -> AtlasRepository,
) : ViewModel() {

    private val repository: AtlasRepository = repositoryFactory(viewModelScope)

    private val endpointText = MutableStateFlow(AtlasEndpoint.DEFAULT.baseUrl)

    /** Problems found before any request is made (bad address, missing permission). */
    private val localFailure = MutableStateFlow<AtlasFailure?>(null)

    val uiState: StateFlow<HomeUiState> = combine(
        repository.connectionState,
        repository.tableSummary,
        repository.failure,
        endpointText,
        localFailure,
    ) { connectionState, tableSummary, repositoryFailure, endpoint, local ->
        val shown = local ?: repositoryFailure
        HomeUiState(
            connectionState = connectionState,
            endpointText = endpoint,
            tableSummary = tableSummary,
            errorMessage = shown?.userMessage,
            errorDetail = shown?.technicalDetail,
            offerAppSettings = shown is AtlasFailure.LocalNetworkPermissionDenied,
        )
    }.stateIn(
        scope = viewModelScope,
        started = SharingStarted.WhileSubscribed(stopTimeoutMillis = 5_000),
        initialValue = HomeUiState(),
    )

    fun onEndpointChanged(text: String) {
        endpointText.value = text
        localFailure.value = null
    }

    /**
     * Connects to the entered endpoint. The Activity calls this only once the
     * platform's local-network permission is granted (or not required).
     */
    fun onConnectClicked() {
        AtlasEndpoint.parse(endpointText.value)
            .onSuccess { endpoint ->
                localFailure.value = null
                endpointText.value = endpoint.baseUrl
                viewModelScope.launch { repository.connect(endpoint) }
            }
            .onFailure { error ->
                localFailure.value = (error as? AtlasException)?.failure ?: AtlasFailure.Unexpected(error.message)
            }
    }

    /** The user declined Android 17's local-network ("Nearby devices") permission. */
    fun onLocalNetworkPermissionDenied() {
        localFailure.value = AtlasFailure.LocalNetworkPermissionDenied
    }

    fun onDisconnectClicked() {
        viewModelScope.launch { repository.disconnect() }
    }

    companion object {
        /**
         * Minimal manual-DI factory: no framework is introduced for one ViewModel.
         * Revisit if/when the dependency graph actually grows past this.
         */
        fun factory(repositoryFactory: (CoroutineScope) -> AtlasRepository): ViewModelProvider.Factory =
            viewModelFactory {
                initializer { HomeViewModel(repositoryFactory) }
            }
    }
}
