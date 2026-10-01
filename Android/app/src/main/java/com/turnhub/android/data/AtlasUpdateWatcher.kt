package com.turnhub.android.data

import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock

/**
 * Atlas has no internet, so the app tells it when newer firmware exists
 * (Documentation/engineering/SIGIL_OTA.md, "Update notice"). While connected,
 * the app reads the release feed at most once per [intervalMs] on the phone's
 * own network and reports the newest version of each product to Atlas
 * (`POST /api/updates/latest`). Atlas compares them with itself and its
 * Sigils and blinks its LED blue while anything is behind. Installing stays
 * the deliberate setup/update flow; this only reports.
 *
 * A report is sent again whenever the feed, the Atlas or its boot changes
 * (Atlas keeps the report in RAM only).
 */
class AtlasUpdateWatcher(
    private val session: AtlasPlayerSession,
    private val releases: FirmwareReleaseSource,
    private val intervalMs: Long,
    private val clock: () -> Long = System::currentTimeMillis,
) {
    private val mutex = Mutex()
    private var feed: FirmwareReleaseFeed? = null
    private var fetchedAtMs: Long? = null
    private var reported: String? = null

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
            if (key == reported) return
            if (session.reportLatestFirmware(endpoint, fields)) reported = key
        } finally {
            mutex.unlock()
        }
    }

    companion object {
        /** Development builds check every minute; release builds once a day. */
        const val DEBUG_INTERVAL_MS = 60_000L
        const val RELEASE_INTERVAL_MS = 24L * 60 * 60 * 1000

        fun reportFields(feed: FirmwareReleaseFeed): List<Pair<String, String>> = listOfNotNull(
            feed.packageFor(FirmwareProduct.ATLAS)?.let { "atlas" to it.version.toString() },
            feed.packageFor(FirmwareProduct.SIGIL_EINK)?.let { "sigilEink" to it.version.toString() },
            feed.packageFor(FirmwareProduct.SIGIL_OLED)?.let { "sigilOled" to it.version.toString() },
        )
    }
}
