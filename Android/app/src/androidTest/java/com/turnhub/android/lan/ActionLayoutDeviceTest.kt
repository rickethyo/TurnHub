package com.turnhub.android.lan

import android.os.SystemClock
import android.view.accessibility.AccessibilityNodeInfo
import androidx.activity.compose.setContent
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.background
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Text
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Rect
import androidx.compose.ui.layout.boundsInWindow
import androidx.compose.ui.layout.onGloballyPositioned
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.unit.Density
import androidx.compose.ui.unit.dp
import androidx.test.core.app.ActivityScenario
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.turnhub.android.MainActivity
import com.turnhub.android.ui.components.*
import com.turnhub.android.ui.theme.*
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith
import java.io.File
import java.util.concurrent.atomic.AtomicReference

@RunWith(AndroidJUnit4::class)
class ActionLayoutDeviceTest {
    private val instrumentation = InstrumentationRegistry.getInstrumentation()
    private fun nodes(n: AccessibilityNodeInfo?): List<AccessibilityNodeInfo> {
        if (n == null) return emptyList()
        n.refresh()
        return listOf(n) + (0 until n.childCount).flatMap { nodes(n.getChild(it)) }
    }
    private fun find(text: String): AccessibilityNodeInfo {
        val until = SystemClock.uptimeMillis() + 6_000
        do {
            if (android.os.Build.VERSION.SDK_INT >= 37) instrumentation.uiAutomation.clearCache()
            nodes(instrumentation.uiAutomation.rootInActiveWindow).firstOrNull { it.text?.toString() == text }?.let { return it }
            SystemClock.sleep(100)
        } while (SystemClock.uptimeMillis() < until)
        error("Missing $text")
    }
    @Test fun actionRolesWrapAndFinishExpansionRevealsItsActionInEveryThemeAtLargerText() {
        val context = instrumentation.targetContext
        var theme by mutableStateOf(TurnHubThemeChoice.GRAPHITE)
        val viewport = AtomicReference<Rect>()
        ActivityScenario.launch(MainActivity::class.java).use { scenario ->
            scenario.onActivity { activity ->
                activity.setTurnScreenOn(true)
                activity.setShowWhenLocked(true)
                activity.window.addFlags(android.view.WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
                activity.setContent {
                TurnHubTheme(theme) {
                    CompositionLocalProvider(LocalDensity provides Density(context.resources.displayMetrics.density, 1.3f)) {
                        Column(Modifier.fillMaxSize().background(palette.bg).safeDrawingPadding().padding(12.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                            Text("Action styles · ${theme.label}", color = palette.text)
                            NavigationRow("Change game", "Magic: The Gathering · Commander") { }
                            FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                                AccentButton("Save setup", {})
                                ToneButton("Cancel", {})
                                ToneButton("Delete setup", {}, tone = Tone.BAD)
                            }
                            key(theme) {
                                Column(Modifier.fillMaxWidth().height(180.dp).onGloballyPositioned { viewport.set(it.boundsInWindow()) }
                                    .verticalScroll(rememberScrollState())) {
                                    Spacer(Modifier.height(130.dp))
                                    DisclosureCard("Finish game") {
                                        ToneButton("End without a winner", {}, Modifier.fillMaxWidth(), tone = Tone.BAD)
                                    }
                                    Spacer(Modifier.height(160.dp))
                                }
                            }
                        }
                    }
                }
            } }
            listOf(TurnHubThemeChoice.GRAPHITE, TurnHubThemeChoice.DAYLIGHT, TurnHubThemeChoice.BRASS, TurnHubThemeChoice.CONTRAST).forEach { choice ->
                scenario.onActivity { theme = choice }
                instrumentation.waitForIdleSync(); SystemClock.sleep(250)
                var header: AccessibilityNodeInfo? = find("Finish game")
                while (header != null && !header.isClickable) header = header.parent
                assertTrue(header?.performAction(AccessibilityNodeInfo.AccessibilityAction.ACTION_SHOW_ON_SCREEN.id) == true)
                SystemClock.sleep(400)
                assertTrue(header?.performAction(AccessibilityNodeInfo.ACTION_CLICK) == true)
                find("End without a winner"); SystemClock.sleep(700)
                val action = find("End without a winner")
                val bounds = android.graphics.Rect().also(action::getBoundsInScreen)
                val visible = requireNotNull(viewport.get())
                assertTrue("Expanded action clipped above in ${choice.label}", bounds.top >= visible.top - 1)
                assertTrue("Expanded action clipped below in ${choice.label}", bounds.bottom <= visible.bottom + 1)
                assertTrue(action.isVisibleToUser)
                val folder = requireNotNull(context.getExternalFilesDir("usability-actions"))
                File(folder, "${choice.key}-large-text.png").outputStream().use {
                    instrumentation.uiAutomation.takeScreenshot().compress(android.graphics.Bitmap.CompressFormat.PNG, 100, it)
                }
            }
        }
    }
}
