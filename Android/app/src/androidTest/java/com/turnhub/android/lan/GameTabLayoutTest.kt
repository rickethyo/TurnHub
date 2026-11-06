package com.turnhub.android.lan

import android.graphics.Rect
import android.os.SystemClock
import android.view.accessibility.AccessibilityNodeInfo
import androidx.activity.compose.setContent
import androidx.compose.foundation.layout.*
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.unit.Density
import androidx.test.core.app.ActivityScenario
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.turnhub.android.MainActivity
import com.turnhub.android.protocol.*
import com.turnhub.android.standalone.*
import com.turnhub.android.ui.components.*
import com.turnhub.android.ui.home.*
import com.turnhub.android.ui.tablet.*
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class GameTabLayoutTest {
    @Test fun largeTextHeaderAndHostGuestLobbyParity() {
        val instrumentation = InstrumentationRegistry.getInstrumentation()
        val context = instrumentation.targetContext
        val table = StandaloneTable(MemoryStandaloneStore(StandaloneGame(), LocalLibrary()))
        table.addPlayer("Layout One"); table.addPlayer("Layout Two")
        val model = LanViewModel(context, table)
        val game = table.state.value.game
        val summary = game.toSummary(System.currentTimeMillis(), com.turnhub.android.domain.TableClock.nowMs())
            .copy(battery = AtlasBattery(100, low = false, charging = true))
        val state = HomeUiState(connectionState = AtlasConnectionState.CONNECTED, tableSummary = summary)
        for (remote in listOf(false, true)) {
            ActivityScenario.launch(MainActivity::class.java).use { scenario ->
                scenario.onActivity { activity ->
                    activity.window.addFlags(android.view.WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
                    activity.setTurnScreenOn(true)
                    activity.setContent {
                    com.turnhub.android.ui.theme.TurnHubTheme {
                        CompositionLocalProvider(LocalDensity provides Density(context.resources.displayMetrics.density, 1.3f)) {
                            Column(Modifier.fillMaxSize().safeDrawingPadding()) {
                                BrandBar(state, false, true, tableBugReportContext(summary, "Layout test"), {}, {})
                                if (!remote) SharedCodeBanner(LanHosting(code = "abc-123"), 1) { }
                                Box(Modifier.weight(1f)) {
                                    if (remote) SharedRoomLobby(LanUiState(remote = game, role = "player", player = 1, online = true), model)
                                    else StandaloneLobby(table.state.value, table, embedded = true)
                                }
                            }
                        }
                    }
                } }
                SystemClock.sleep(800)
                fun collect(n: AccessibilityNodeInfo?): List<AccessibilityNodeInfo> = if (n == null) emptyList() else
                    listOf(n) + (0 until n.childCount).flatMap { collect(n.getChild(it)) }
                val deadline = SystemClock.uptimeMillis() + 8000
                var nodes = collect(instrumentation.uiAutomation.rootInActiveWindow)
                while (nodes.none { it.text?.toString()?.equals("1. GAME", true) == true } && SystemClock.uptimeMillis() < deadline) {
                    SystemClock.sleep(200)
                    nodes = collect(instrumentation.uiAutomation.rootInActiveWindow)
                }
                val capture = java.io.File(context.getExternalFilesDir(null), if (remote) "game-tab-guest-top.png" else "game-tab-host-top.png")
                capture.outputStream().use { instrumentation.uiAutomation.takeScreenshot().compress(android.graphics.Bitmap.CompressFormat.PNG, 100, it) }
                val battery = nodes.first { it.text?.toString() == "100%" }
                val rect = Rect().also(battery::getBoundsInScreen)
                assertTrue("Battery should fit one line: $rect", rect.width() > rect.height())
                val gameHeading = nodes.first { it.text?.toString()?.equals("1. GAME", true) == true }
                val gameRect = Rect().also(gameHeading::getBoundsInScreen)
                assertTrue("Game should begin near the top: $gameRect", gameRect.top < context.resources.displayMetrics.heightPixels * .42)
                val texts = nodes.mapNotNull { it.text?.toString() }.toMutableSet()
                repeat(12) {
                    if (texts.none { it == "Start game" } || texts.none { it.startsWith("Rules") }) {
                        collect(instrumentation.uiAutomation.rootInActiveWindow).firstOrNull { it.isScrollable }
                            ?.performAction(AccessibilityNodeInfo.ACTION_SCROLL_FORWARD)
                        SystemClock.sleep(200)
                        texts += collect(instrumentation.uiAutomation.rootInActiveWindow).mapNotNull { it.text?.toString() }
                    }
                }
                val joined = texts.joinToString(" / ")
                for (label in listOf("1. GAME", "Game mode", "Planechase", "Add Guest", "Add profile", "Start game"))
                    assertTrue("Missing $label: $joined", texts.any { it.equals(label, ignoreCase = true) })
                assertTrue(joined, texts.any { it.startsWith("Rules") })
                assertFalse(joined, texts.contains("Advanced settings"))
                val path = java.io.File(context.getExternalFilesDir(null), if (remote) "game-tab-guest.png" else "game-tab-host.png")
                path.outputStream().use { instrumentation.uiAutomation.takeScreenshot().compress(android.graphics.Bitmap.CompressFormat.PNG, 100, it) }
            }
        }
    }
}
