package com.turnhub.android.ui.tablet

import android.graphics.Bitmap
import android.os.SystemClock
import androidx.activity.compose.setContent
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.safeDrawingPadding
import androidx.compose.ui.Modifier
import androidx.compose.material3.Surface
import androidx.test.core.app.ActivityScenario
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.turnhub.android.MainActivity
import com.turnhub.android.data.TabletSeat
import com.turnhub.android.standalone.*
import com.turnhub.android.ui.theme.TurnHubTheme
import kotlinx.coroutines.runBlocking
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith
import java.io.File

/** Isolated rendering: never assigns an owner role or edits the device's saved game. */
@RunWith(AndroidJUnit4::class)
class AdminAndHistoryDeviceTest {
    @Test fun requiredAdminAndPairedHistorySyncStayVisible() {
        val instrumentation = InstrumentationRegistry.getInstrumentation()
        val context = instrumentation.targetContext
        val table = StandaloneTable(MemoryStandaloneStore(StandaloneGame(), LocalLibrary()), shortGameMs = 0)
        table.createAccount("Test owner")
        fun text(node: android.view.accessibility.AccessibilityNodeInfo?): String = if (node == null) "" else
            node.text?.toString().orEmpty() + " " + (0 until node.childCount).joinToString { text(node.getChild(it)) }
        fun awaitText(expected: String): String {
            val end = SystemClock.uptimeMillis() + 5000
            while (!text(instrumentation.uiAutomation.rootInActiveWindow).contains(expected) && SystemClock.uptimeMillis() < end) SystemClock.sleep(150)
            return text(instrumentation.uiAutomation.rootInActiveWindow).also { assertTrue(it, it.contains(expected)) }
        }
        fun clickText(expected: String) {
            fun find(node: android.view.accessibility.AccessibilityNodeInfo?): android.view.accessibility.AccessibilityNodeInfo? {
                if (node == null) return null
                if (node.text?.toString() == expected) return node
                for (i in 0 until node.childCount) find(node.getChild(i))?.let { return it }
                return null
            }
            var node = requireNotNull(find(instrumentation.uiAutomation.rootInActiveWindow))
            while (!node.isClickable && node.parent != null) node = node.parent
            assertTrue(node.performAction(android.view.accessibility.AccessibilityNodeInfo.ACTION_CLICK))
            instrumentation.waitForIdleSync()
        }
        fun screenshot(name: String) {
            File(context.getExternalFilesDir(null), name).outputStream().use {
                instrumentation.uiAutomation.takeScreenshot().compress(Bitmap.CompressFormat.PNG, 100, it)
            }
        }
        ActivityScenario.launch(MainActivity::class.java).use { scenario ->
            scenario.onActivity { activity -> activity.setContent {
                TurnHubTheme { Surface { DeviceAdminSetupDialog(table.state.value.library, table) } }
            } }
            assertFalse(awaitText("Set up your Admin account").contains("Not now"))
            screenshot("required-admin.png")
            table.addPlayer("Test owner"); table.addGuest("Test guest"); table.start()
            runBlocking { table.control(TabletSeat(StandaloneGame.seatHandle(0).id, 1), "win") }
            val record = table.state.value.library.history.single()
            val library = table.state.value.library.withDelivery(MatchDelivery(record.recordId, DeliveryStatus.REJECTED,
                reason = "Test retry", atlasId = "atlas", profileIds = listOf("ABCDEF01", "")))
            val state = table.state.value.copy(library = library)
            val syncs = java.util.concurrent.atomic.AtomicInteger()
            val retries = java.util.concurrent.atomic.AtomicInteger()
            // Paired access needs no signed-in profile to expose History controls.
            scenario.onActivity { activity -> activity.setContent {
                TurnHubTheme { Surface(Modifier.fillMaxSize().safeDrawingPadding()) {
                    LocalLibraryScreen(state, onClose = {}, canImport = true,
                        importState = LocalImportState(atlasId = "atlas", paired = true),
                        embeddedSection = LibrarySection.GAMES, table = table,
                        onLoadProfiles = { syncs.incrementAndGet() }, onImport = { id, mapping ->
                            assertEquals(record.recordId, id)
                            assertEquals(listOf("ABCDEF01", ""), mapping)
                            retries.incrementAndGet()
                        })
                } }
            } }
            awaitText("Sync games with Atlas")
            clickText("Sync games with Atlas")
            assertEquals(1, syncs.get())
            clickText("Details and import")
            awaitText("Retry Atlas copy")
            clickText("Retry Atlas copy")
            assertEquals(1, retries.get())
            screenshot("paired-history-sync.png")
        }
    }
}
