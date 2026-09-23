package com.turnhub.android.data

import com.turnhub.android.protocol.AtlasConnectionState
import com.turnhub.android.protocol.GameProfile
import com.turnhub.android.protocol.Player
import com.turnhub.android.protocol.Sigil
import com.turnhub.android.protocol.TableSettings
import com.turnhub.android.protocol.TableState
import com.turnhub.android.protocol.TableSummary
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

/**
 * An in-memory stand-in for a real Atlas connection.
 *
 * No HTTP, Bluetooth, or discovery happens here -- this milestone is UI-only.
 * It exists so the UI/ViewModel layer can be built and exercised against
 * something that behaves like the eventual real [AtlasRepository], including a
 * believable connect delay and mock values loosely modeled on
 * protocol/examples/running.response.json.
 */
class MockAtlasRepository : AtlasRepository {

    private val _connectionState = MutableStateFlow(AtlasConnectionState.DISCONNECTED)
    override val connectionState: StateFlow<AtlasConnectionState> = _connectionState.asStateFlow()

    private val _tableSummary = MutableStateFlow<TableSummary?>(null)
    override val tableSummary: StateFlow<TableSummary?> = _tableSummary.asStateFlow()

    private val _sigils = MutableStateFlow<List<Sigil>>(emptyList())
    override val sigils: StateFlow<List<Sigil>> = _sigils.asStateFlow()

    override suspend fun connect() {
        if (_connectionState.value != AtlasConnectionState.DISCONNECTED) return
        _connectionState.value = AtlasConnectionState.CONNECTING
        delay(CONNECT_DELAY_MS)
        _tableSummary.value = mockTableSummary()
        _sigils.value = mockSigils()
        _connectionState.value = AtlasConnectionState.CONNECTED
    }

    override suspend fun disconnect() {
        _connectionState.value = AtlasConnectionState.DISCONNECTED
        _tableSummary.value = null
        _sigils.value = emptyList()
    }

    private fun mockTableSummary(): TableSummary = TableSummary(
        atlasId = "THA-025448410001",
        firmwareVersion = "0.6.0-dev",
        revision = 6,
        state = TableState.RUNNING,
        settings = TableSettings(profile = GameProfile.GENERIC, startingLife = 40),
        activePlayerNumber = 2,
        players = listOf(
            Player(
                playerNumber = 1,
                moduleId = 8,
                slot = 1,
                profileId = "A1B2C3D4",
                displayName = "Ricky",
                eliminated = false,
                life = 40,
                turnsCompleted = 1,
            ),
            Player(
                playerNumber = 2,
                moduleId = 9,
                slot = 1,
                profileId = null,
                displayName = "Guest",
                eliminated = false,
                life = 34,
                turnsCompleted = 0,
            ),
        ),
    )

    private fun mockSigils(): List<Sigil> = listOf(
        Sigil(id = "SIGIL-01", name = "Sigil 1", moduleId = 8, connected = true, assignedPlayerNumber = 1),
        Sigil(id = "SIGIL-02", name = "Sigil 2", moduleId = 9, connected = true, assignedPlayerNumber = 2),
        Sigil(id = "SIGIL-03", name = "Sigil 3", moduleId = 10, connected = false, assignedPlayerNumber = null),
    )

    private companion object {
        const val CONNECT_DELAY_MS = 900L
    }
}
