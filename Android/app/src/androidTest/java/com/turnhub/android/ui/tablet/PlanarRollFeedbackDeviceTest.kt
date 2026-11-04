package com.turnhub.android.ui.tablet

import android.graphics.Bitmap
import android.os.SystemClock
import androidx.activity.compose.setContent
import androidx.compose.material3.MaterialTheme
import androidx.test.core.app.ActivityScenario
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.turnhub.android.MainActivity
import com.turnhub.android.lan.PlanarRollFeedback
import com.turnhub.android.standalone.PlanarFace
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import java.io.File

/** Render feedback without a room, network command or mutation of any game. */
@RunWith(AndroidJUnit4::class)
class PlanarRollFeedbackDeviceTest {
    @Test fun rendersPendingConfirmedRejectedAndUncertainRolls() {
        val instrumentation = InstrumentationRegistry.getInstrumentation()
        val context = instrumentation.targetContext
        val output = File(context.getExternalFilesDir(null), "planar-feedback-qa").apply { mkdirs() }
        val cases = listOf(
            Triple("pending", PlanarRollFeedback("pending", "pending"), "Waiting for the host"),
            Triple("blank", PlanarRollFeedback("blank", "confirmed", PlanarFace.BLANK), "Blank. No planar effect."),
            Triple("chaos", PlanarRollFeedback("chaos", "confirmed", PlanarFace.CHAOS), "Chaos ensues!"),
            Triple("planeswalk", PlanarRollFeedback("planeswalk", "confirmed", PlanarFace.PLANESWALK), "host advanced"),
            Triple("uncertain", PlanarRollFeedback("uncertain", "uncertain"), "Check same roll"),
            Triple("rejected", PlanarRollFeedback("rejected", "rejected"), "Roll not applied"),
        )
        ActivityScenario.launch(MainActivity::class.java).use { scenario ->
            for ((id, feedback, expected) in cases) {
                scenario.onActivity { activity -> activity.setContent {
                    MaterialTheme {
                        RemotePlanarRollDialog(id, feedback, false, feedback.status == "uncertain", false,
                            onRetry = {}, onClose = {}, onShowPlanes = {})
                    }
                } }
                var found = false
                val end = SystemClock.uptimeMillis() + 4_000
                while (!found && SystemClock.uptimeMillis() < end) {
                    SystemClock.sleep(150)
                    fun text(node: android.view.accessibility.AccessibilityNodeInfo?): String = if (node == null) "" else
                        node.text?.toString().orEmpty() + " " + (0 until node.childCount).joinToString { text(node.getChild(it)) }
                    found = text(instrumentation.uiAutomation.rootInActiveWindow).contains(expected)
                }
                assertTrue("Missing feedback: $id / $expected", found)
                File(output, "$id.png").outputStream().use {
                    instrumentation.uiAutomation.takeScreenshot().compress(Bitmap.CompressFormat.PNG, 100, it)
                }
            }
            scenario.onActivity { activity -> activity.setContent {
                MaterialTheme { RemotePlanarRollDialog("reduced", PlanarRollFeedback("reduced", "pending"), true,
                    false, false, onRetry = {}, onClose = {}, onShowPlanes = {}) }
            } }
            SystemClock.sleep(250)
            File(output, "reduced-motion.png").outputStream().use {
                instrumentation.uiAutomation.takeScreenshot().compress(Bitmap.CompressFormat.PNG, 100, it)
            }
        }
    }
}
