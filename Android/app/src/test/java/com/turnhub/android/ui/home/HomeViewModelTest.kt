package com.turnhub.android.ui.home

import com.turnhub.android.data.AtlasEndpoint
import com.turnhub.android.data.AtlasFailure
import com.turnhub.android.data.AtlasRepository
import com.turnhub.android.domain.TableSummary
import com.turnhub.android.protocol.AtlasConnectionState
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.test.UnconfinedTestDispatcher
import kotlinx.coroutines.test.resetMain
import kotlinx.coroutines.test.runTest
import kotlinx.coroutines.test.setMain
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test

private class RecordingRepository : AtlasRepository {
    override val connectionState = MutableStateFlow(AtlasConnectionState.DISCONNECTED)
    override val endpoint = MutableStateFlow<AtlasEndpoint?>(null)
    override val tableSummary = MutableStateFlow<TableSummary?>(null)
    override val failure = MutableStateFlow<AtlasFailure?>(null)
    val connects = mutableListOf<AtlasEndpoint>()
    var disconnects = 0

    override suspend fun connect(endpoint: AtlasEndpoint) {
        connects += endpoint
    }

    override suspend fun disconnect() {
        disconnects++
    }
}

@OptIn(ExperimentalCoroutinesApi::class)
class HomeViewModelTest {

    private val repository = RecordingRepository()

    @Before
    fun setUp() = Dispatchers.setMain(UnconfinedTestDispatcher())

    @After
    fun tearDown() = Dispatchers.resetMain()

    @Test
    fun `defaults to the Atlas access point and connects to it`() = runTest {
        val viewModel = HomeViewModel { repository }
        backgroundScope.launch(UnconfinedTestDispatcher(testScheduler)) { viewModel.uiState.collect {} }

        assertEquals("http://192.168.4.1", viewModel.uiState.value.endpointText)
        viewModel.onConnectClicked()

        assertEquals(listOf(AtlasEndpoint.DEFAULT), repository.connects)
    }

    @Test
    fun `edited endpoint is normalized and used`() = runTest {
        val viewModel = HomeViewModel { repository }
        backgroundScope.launch(UnconfinedTestDispatcher(testScheduler)) { viewModel.uiState.collect {} }

        viewModel.onEndpointChanged("10.0.0.5:8080/")
        viewModel.onConnectClicked()

        assertEquals("http://10.0.0.5:8080", repository.connects.single().baseUrl)
        assertEquals("http://10.0.0.5:8080", viewModel.uiState.value.endpointText)
    }

    @Test
    fun `invalid endpoint shows an error and does not connect`() = runTest {
        val viewModel = HomeViewModel { repository }
        backgroundScope.launch(UnconfinedTestDispatcher(testScheduler)) { viewModel.uiState.collect {} }

        viewModel.onEndpointChanged("https://192.168.4.1")
        viewModel.onConnectClicked()

        assertTrue(repository.connects.isEmpty())
        assertTrue(viewModel.uiState.value.errorMessage!!.contains("http://"))

        viewModel.onEndpointChanged("192.168.4.1")
        assertNull(viewModel.uiState.value.errorMessage)
    }

    @Test
    fun `denied local network permission explains itself and offers settings`() = runTest {
        val viewModel = HomeViewModel { repository }
        backgroundScope.launch(UnconfinedTestDispatcher(testScheduler)) { viewModel.uiState.collect {} }

        viewModel.onLocalNetworkPermissionDenied()

        val state = viewModel.uiState.value
        assertTrue(state.errorMessage!!.contains("Nearby devices"))
        assertTrue(state.offerAppSettings)
        assertTrue(repository.connects.isEmpty())

        // Granted later: connecting clears the message.
        viewModel.onConnectClicked()
        assertNull(viewModel.uiState.value.errorMessage)
        assertEquals(false, viewModel.uiState.value.offerAppSettings)
        assertEquals(1, repository.connects.size)
    }

    @Test
    fun `repository failures are shown and editing is locked while connected`() = runTest {
        val viewModel = HomeViewModel { repository }
        backgroundScope.launch(UnconfinedTestDispatcher(testScheduler)) { viewModel.uiState.collect {} }

        repository.connectionState.value = AtlasConnectionState.CONNECTED
        repository.failure.value = AtlasFailure.Timeout("SocketTimeoutException: test")

        val state = viewModel.uiState.value
        assertEquals(repository.failure.value!!.userMessage, state.errorMessage)
        assertEquals("SocketTimeoutException: test", state.errorDetail)
        assertTrue(state.isRetrying)
        assertEquals(false, state.endpointEditable)

        viewModel.onDisconnectClicked()
        assertEquals(1, repository.disconnects)
    }
}
