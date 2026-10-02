package com.turnhub.android.data

import com.turnhub.android.protocol.AccessibilitySettings
import com.turnhub.android.protocol.ControlResult
import com.turnhub.android.protocol.GameSettingsInfo
import com.turnhub.android.protocol.LedStyle
import com.turnhub.android.protocol.LoginResult
import com.turnhub.android.protocol.ProfileSummary
import com.turnhub.android.protocol.SessionInfo
import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertEquals
import org.junit.Test
import java.io.IOException

private class ReportingAtlas : AtlasSessionTransport {
    val reports = mutableListOf<List<Pair<String, String>>>()
    var refuse = false
    var behind = 0
    override suspend fun reportLatestFirmware(fields: List<Pair<String, String>>): Int {
        if (refuse) throw AtlasException(AtlasFailure.Rejected("no"))
        reports += fields
        return behind
    }
    override suspend fun getProfiles(): List<ProfileSummary> = emptyList()
    override suspend fun login(profileId: String, pin: String): LoginResult = throw AtlasException(AtlasFailure.Rejected("no"))
    override suspend fun me(token: String): SessionInfo = throw AtlasException(AtlasFailure.Rejected("no"))
    override suspend fun join(token: String): String? = null
    override suspend fun control(token: String, action: ControlAction, expectedRevision: Long?, expectedBootId: String?) =
        ControlResult(true, "ACCEPTED", null, null, null)
    override suspend fun getGameSettings(token: String): GameSettingsInfo = throw AtlasException(AtlasFailure.Rejected("no"))
    override suspend fun setTurnTimer(token: String, turnTimerMs: Long): String? = null
    override suspend fun getAccessibility(token: String): AccessibilitySettings = throw AtlasException(AtlasFailure.Rejected("no"))
    override suspend fun saveAccessibility(token: String, sigilSound: Boolean, ledStyle: LedStyle, longPressMs: Int, winHoldMs: Int) =
        throw AtlasException(AtlasFailure.Rejected("no"))
    override suspend fun logout(token: String) {}
}

private class CountingReleases(var feed: FirmwareReleaseFeed?) : FirmwareReleaseSource {
    var reads = 0
    override suspend fun latestFeed(): FirmwareReleaseFeed {
        reads++
        return feed ?: throw IOException("no internet")
    }
    override suspend fun download(pkg: FirmwarePackage, onProgress: (Long) -> Unit): ByteArray = ByteArray(0)
}

class AtlasUpdateWatcherTest {

    private fun pkg(product: FirmwareProduct, version: String) =
        FirmwarePackage(product, FirmwareVersion.parse(version)!!, "${product.wire}.thfw", 1000, "0".repeat(64))

    private val feed = FirmwareReleaseFeed(
        "v0.9.4",
        listOf(pkg(FirmwareProduct.ATLAS, "0.6.4"), pkg(FirmwareProduct.SIGIL_EINK, "0.9.4"), pkg(FirmwareProduct.SIGIL_OLED, "0.9.4")),
    )

    private val atlas = ReportingAtlas()
    private val session = AtlasPlayerSession { atlas }
    private var now = 0L
    private val releases = CountingReleases(feed)
    private val watcher = AtlasUpdateWatcher(session, releases, intervalMs = 60_000, clock = { now }, refreshMs = Long.MAX_VALUE)
    private val endpoint = AtlasEndpoint.DEFAULT

    @Test
    fun `reports once per Atlas boot and reads the feed once per interval`() = runTest {
        watcher.onTick(endpoint, "BOOT1")
        assertEquals(listOf(listOf("atlas" to "0.6.4", "sigilEink" to "0.9.4", "sigilOled" to "0.9.4")), atlas.reports)
        now = 30_000
        watcher.onTick(endpoint, "BOOT1")
        assertEquals(1, releases.reads)
        assertEquals(1, atlas.reports.size)
        // Atlas restarted: it forgot the report, so it hears it again.
        watcher.onTick(endpoint, "BOOT2")
        assertEquals(2, atlas.reports.size)
        // The next interval reads the feed again; an unchanged feed isn't resent.
        now = 60_000
        watcher.onTick(endpoint, "BOOT2")
        assertEquals(2, releases.reads)
        assertEquals(2, atlas.reports.size)
    }

    @Test
    fun `a new release is reported and a failed report is retried`() = runTest {
        atlas.refuse = true
        watcher.onTick(endpoint, "BOOT1")
        assertEquals(0, atlas.reports.size)
        atlas.refuse = false
        watcher.onTick(endpoint, "BOOT1")
        assertEquals(1, atlas.reports.size)
        releases.feed = FirmwareReleaseFeed("v0.9.5", listOf(pkg(FirmwareProduct.ATLAS, "0.6.5")))
        now = 60_000
        watcher.onTick(endpoint, "BOOT1")
        assertEquals(listOf("atlas" to "0.6.5"), atlas.reports.last())
    }

    @Test
    fun `no internet keeps the last feed and sends nothing new`() = runTest {
        releases.feed = null
        watcher.onTick(endpoint, "BOOT1")
        assertEquals(0, atlas.reports.size)
        releases.feed = feed
        now = 60_000
        watcher.onTick(endpoint, "BOOT1")
        assertEquals(1, atlas.reports.size)
    }

    @Test
    fun `Atlas's count of devices behind is kept fresh for the banner`() = runTest {
        val refreshing = AtlasUpdateWatcher(session, releases, intervalMs = 60_000, clock = { now }, refreshMs = 30_000)
        atlas.behind = 2
        refreshing.onTick(endpoint, "BOOT1")
        assertEquals(2, refreshing.available.value)
        // A Sigil updated: the next refresh hears the new count, with no new feed or boot.
        atlas.behind = 1
        now = 10_000
        refreshing.onTick(endpoint, "BOOT1")
        assertEquals(2, refreshing.available.value)
        now = 30_000
        refreshing.onTick(endpoint, "BOOT1")
        assertEquals(1, refreshing.available.value)
        assertEquals(2, atlas.reports.size)
        atlas.behind = 0
        now = 60_000
        refreshing.onTick(endpoint, "BOOT1")
        assertEquals(0, refreshing.available.value)
    }

    @Test
    fun `a requested refresh reads Atlas's count on the next tick`() = runTest {
        atlas.behind = 1
        watcher.onTick(endpoint, "BOOT1")
        assertEquals(1, watcher.available.value)
        // An update finished: no waiting for the refresh interval.
        atlas.behind = 0
        watcher.requestRefresh()
        watcher.onTick(endpoint, "BOOT1")
        assertEquals(0, watcher.available.value)
        assertEquals(2, atlas.reports.size)
        watcher.onTick(endpoint, "BOOT1")
        assertEquals(2, atlas.reports.size)
    }
}
