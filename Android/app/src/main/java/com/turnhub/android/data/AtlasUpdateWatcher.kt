package com.turnhub.android.data

import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock

/**
 * Atlas has no internet, so the app tells it when newer firmware exists
 * (Documentation/engineering/FIRMWARE_UPDATES.md, "Update notice"). While connected,
 * the app reads the release feed at most once per [intervalMs] on the phone's
 * own network and reports the newest version of each product to Atlas
 * (`POST /api/updates/latest`). Atlas compares them with itself and its
 * Sigils and blinks its LED blue while anything is behind. Installing stays
 * the deliberate setup/update flow; this only reports.
 *
 * A report is sent again whenever the feed, the Atlas or its boot changes
 * (Atlas keeps the report in RAM only), and every [refreshMs] besides: Atlas's
 * answer says how many devices are behind, which changes as Sigils join or
 * update, and the app's "Update available" banner follows it ([available]).
 */
class AtlasUpdateWatcher(
    private val session: AtlasPlayerSession,
    private val releases: FirmwareReleaseSource,
    private val intervalMs: Long,
    private val clock: () -> Long = System::currentTimeMillis,
    private val refreshMs: Long = REFRESH_MS,
) {
    private val _available = MutableStateFlow(0)

    /** Devices Atlas counts as running older firmware than the last report. */
    val available: StateFlow<Int> = _available.asStateFlow()

    private val mutex = Mutex()
    private var feed: FirmwareReleaseFeed? = null
    private var fetchedAtMs: Long? = null
    private var reported: String? = null
    private var reportedAtMs = 0L
    @Volatile private var refreshNow = false

    /**
     * Read Atlas's count again on the next tick, rather than after
     * [refreshMs]: after an update ran, or when it found nothing to install.
     */
    fun requestRefresh() {
        refreshNow = true
    }

    /**
     * Called on every table update while connected; cheap unless a check is
     * due. A tick that arrives while one is running is skipped.
     */
    suspend fun onTick(endpoint: AtlasEndpoint, bootId: String) {
        if (!mutex.tryLock()) return
        try {
            val now = clock()
            val last = fetchedAtMs
            if (last == null || now - last >= intervalMs) {
                fetchedAtMs = now
                feed = try {
                    releases.latestFeed()
                } catch (e: CancellationException) {
                    throw e
                } catch (_: Exception) {
                    feed // No internet or no release yet: keep the last one, try again next interval.
                }
            }
            val current = feed ?: return
            val fields = reportFields(current)
            if (fields.isEmpty()) return
            val key = "${endpoint.baseUrl}|$bootId|$fields"
            if (key == reported && !refreshNow && now - reportedAtMs < refreshMs) return
            val behind = session.reportLatestFirmware(endpoint, fields) ?: return
            reported = key
            reportedAtMs = now
            refreshNow = false
            _available.value = behind
        } finally {
            mutex.unlock()
        }
    }

    companion object {
        /** Development builds check every minute; release builds once a day. */
        const val DEBUG_INTERVAL_MS = 60_000L
        const val RELEASE_INTERVAL_MS = 24L * 60 * 60 * 1000

        /** The report is repeated this often, to learn how many devices are behind now. */
        const val REFRESH_MS = 30_000L

        fun reportFields(feed: FirmwareReleaseFeed): List<Pair<String, String>> = listOfNotNull(
            feed.packageFor(FirmwareProduct.ATLAS)?.let { "atlas" to it.version.toString() },
            feed.packageFor(FirmwareProduct.SIGIL_EINK)?.let { "sigilEink" to it.version.toString() },
            feed.packageFor(FirmwareProduct.SIGIL_OLED)?.let { "sigilOled" to it.version.toString() },
        )
    }
}
