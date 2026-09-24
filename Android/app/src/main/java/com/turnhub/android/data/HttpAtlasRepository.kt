package com.turnhub.android.data

import com.turnhub.android.domain.SeatKey
import com.turnhub.android.domain.TableClock
import com.turnhub.android.domain.TableSummary
import com.turnhub.android.domain.TableSummaryMapper
import com.turnhub.android.protocol.AtlasConnectionState
import com.turnhub.android.protocol.AtlasInfo
import com.turnhub.android.protocol.StateSnapshot
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch

/**
 * Live, read-only view of one Atlas over its HTTP API.
 *
 * Connection flow: `GET /api/v1/info` -> compatibility check ->
 * `GET /api/v1/state` -> map -> CONNECTED -> poll state every [pollIntervalMs].
 *
 * Reconnect rules follow protocol/http-v1.md, keyed on (atlasId, bootId,
 * revision):
 * - Every snapshot replaces the summary whole, even at the same revision,
 *   because Atlas's sampled clocks move without a revision change.
 * - A changed atlasId (different Atlas), changed bootId (Atlas restarted) or a
 *   lower revision (including wrap) discards the cached view and rebuilds from
 *   a fresh info + state handshake. Nothing is merged across epochs.
 * - After [maxConsecutivePollFailures] failed polls the live view is dropped
 *   and the repository returns to DISCONNECTED; the next [connect] starts from
 *   a fresh snapshot. This read-only milestone sends no commands, so there is
 *   nothing that could be replayed.
 *
 * All work runs in [scope]; cancelling it stops polling. Each connection gets a
 * generation number and may only publish while it is still current, so a
 * request that completes after [disconnect] can never resurrect stale state.
 */
class HttpAtlasRepository(
    private val transportFactory: AtlasTransportFactory,
    private val scope: CoroutineScope,
    private val pollIntervalMs: Long = 1_000,
    private val maxConsecutivePollFailures: Int = 3,
    /** Upper bound on polls between `/api/seats` name refreshes. */
    private val nameRefreshPolls: Int = 5,
    /** Local monotonic clock (ms), stamped on each summary so the UI can tick between polls. */
    private val clock: () -> Long = TableClock::nowMs,
) : AtlasRepository {

    private val _connectionState = MutableStateFlow(AtlasConnectionState.DISCONNECTED)
    override val connectionState: StateFlow<AtlasConnectionState> = _connectionState.asStateFlow()

    private val _endpoint = MutableStateFlow<AtlasEndpoint?>(null)
    override val endpoint: StateFlow<AtlasEndpoint?> = _endpoint.asStateFlow()

    private val _tableSummary = MutableStateFlow<TableSummary?>(null)
    override val tableSummary: StateFlow<TableSummary?> = _tableSummary.asStateFlow()

    private val _failure = MutableStateFlow<AtlasFailure?>(null)
    override val failure: StateFlow<AtlasFailure?> = _failure.asStateFlow()

    private val lock = Any()
    private var generation = 0L
    private var session: Job? = null

    /** The info a summary was built from; needed to remap later snapshots of the same boot. */
    private class Live(
        val info: AtlasInfo,
        val summary: TableSummary,
        val names: Map<SeatKey, String>,
        val pollsSinceNames: Int,
    )

    override suspend fun connect(endpoint: AtlasEndpoint) {
        val settled = CompletableDeferred<Unit>()
        synchronized(lock) {
            if (_connectionState.value != AtlasConnectionState.DISCONNECTED) return
            val connection = ++generation
            _endpoint.value = endpoint
            _failure.value = null
            _tableSummary.value = null
            _connectionState.value = AtlasConnectionState.CONNECTING
            session = scope.launch {
                try {
                    runSession(connection, transportFactory.create(endpoint), settled)
                } finally {
                    settled.complete(Unit)
                }
            }
        }
        settled.await()
    }

    override suspend fun disconnect() {
        val ending = synchronized(lock) {
            generation++
            _connectionState.value = AtlasConnectionState.DISCONNECTED
            _tableSummary.value = null
            _failure.value = null
            session.also { session = null }
        }
        ending?.cancel()
    }

    private suspend fun runSession(
        connection: Long,
        transport: AtlasTransport,
        settled: CompletableDeferred<Unit>,
    ) {
        var live = try {
            call { handshake(transport) }
        } catch (e: AtlasException) {
            drop(connection, e.failure)
            return
        }
        val published = publish(connection) {
            _tableSummary.value = live.summary
            _connectionState.value = AtlasConnectionState.CONNECTED
        }
        settled.complete(Unit)
        if (!published) return

        var failures = 0
        while (true) {
            delay(pollIntervalMs)
            try {
                live = call { refresh(transport, live) }
                failures = 0
                if (!publish(connection) { _tableSummary.value = live.summary; _failure.value = null }) return
            } catch (e: AtlasException) {
                failures++
                if (failures >= maxConsecutivePollFailures) {
                    drop(connection, AtlasFailure.LostConnection(e.failure))
                    return
                }
                if (!publish(connection) { _failure.value = e.failure }) return
            }
        }
    }

    private suspend fun refresh(transport: AtlasTransport, live: Live): Live {
        val snapshot = call { transport.getState() }
        AtlasCompatibility.requireCompatible(snapshot)
        val current = live.summary
        val sameEpoch = snapshot.atlasId == current.atlasId && snapshot.bootId == current.bootId
        if (!sameEpoch || snapshot.revision < current.revision) {
            // Different Atlas, new boot, or revision went backwards: start over.
            return handshake(transport)
        }
        // Names aren't versioned by revision: refresh them when membership may
        // have changed (any revision change) and periodically otherwise.
        val refreshNames = snapshot.revision != current.revision ||
            live.pollsSinceNames + 1 >= nameRefreshPolls
        val names = if (refreshNames) fetchSeatNames(transport) else null
        return Live(
            info = live.info,
            summary = map(live.info, snapshot, names ?: live.names),
            names = names ?: live.names,
            pollsSinceNames = if (names != null) 0 else live.pollsSinceNames + 1,
        )
    }

    /** info -> compatibility -> state (+ names), retried once if Atlas restarted in between. */
    private suspend fun handshake(transport: AtlasTransport): Live {
        repeat(2) {
            val info = call { transport.getInfo() }
            AtlasCompatibility.requireCompatible(info)
            val snapshot: StateSnapshot = call { transport.getState() }
            AtlasCompatibility.requireCompatible(snapshot)
            if (snapshot.atlasId == info.atlasId && snapshot.bootId == info.bootId) {
                val names = fetchSeatNames(transport).orEmpty()
                return Live(info, map(info, snapshot, names), names, pollsSinceNames = 0)
            }
        }
        throw AtlasException(AtlasFailure.Malformed("Atlas identity changed while connecting; try again"))
    }

    /** Seat names are presentation only: any failure just keeps the names we have. */
    private suspend fun fetchSeatNames(transport: AtlasTransport): Map<SeatKey, String>? = try {
        transport.getSeats()
            .mapNotNull { seat -> seat.name?.let { SeatKey(seat.moduleId, seat.slot) to it } }
            .toMap()
    } catch (e: CancellationException) {
        throw e
    } catch (_: Exception) {
        null
    }

    private fun map(info: AtlasInfo, snapshot: StateSnapshot, names: Map<SeatKey, String>) =
        TableSummaryMapper.map(info, snapshot, names, receivedAtMs = clock())

    /**
     * Normalizes anything thrown below this repository into [AtlasException],
     * keeping cancellation intact, so no failure can escape [scope] and crash
     * the app.
     */
    private suspend fun <T> call(block: suspend () -> T): T = try {
        block()
    } catch (e: CancellationException) {
        throw e
    } catch (e: AtlasException) {
        throw e
    } catch (e: Exception) {
        throw AtlasException(AtlasFailure.Unexpected("${e.javaClass.simpleName}: ${e.message}"))
    }

    private fun drop(connection: Long, failure: AtlasFailure) {
        publish(connection) {
            _connectionState.value = AtlasConnectionState.DISCONNECTED
            _tableSummary.value = null
            _failure.value = failure
            session = null
        }
    }

    /** Applies [update] only if [connection] is still the current one. */
    private inline fun publish(connection: Long, update: () -> Unit): Boolean = synchronized(lock) {
        if (connection != generation) return false
        update()
        true
    }
}
