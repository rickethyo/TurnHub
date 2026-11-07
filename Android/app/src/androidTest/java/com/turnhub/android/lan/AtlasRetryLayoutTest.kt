package com.turnhub.android.lan

import android.graphics.Rect
import android.os.SystemClock
import android.view.accessibility.AccessibilityNodeInfo
import androidx.activity.compose.setContent
import androidx.compose.runtime.*
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.unit.Density
import androidx.test.core.app.ActivityScenario
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.turnhub.android.MainActivity
import com.turnhub.android.data.HostedAtlasLink
import com.turnhub.android.protocol.*
import com.turnhub.android.standalone.*
import com.turnhub.android.ui.home.*
import com.turnhub.android.ui.tablet.*
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class AtlasRetryLayoutTest {
    @Test fun retryAndOfflineFeedbackKeepTheGameAndBatteryInPlace() {
        val instrumentation = InstrumentationRegistry.getInstrumentation()
        val context = instrumentation.targetContext
        val table = StandaloneTable(MemoryStandaloneStore(StandaloneGame(), LocalLibrary()))
        val summary = table.state.value.game.toSummary(System.currentTimeMillis(), com.turnhub.android.domain.TableClock.nowMs())
            .copy(battery = AtlasBattery(100, false, true))
        val connected = HomeUiState(connectionState = AtlasConnectionState.CONNECTED, tableSummary = summary)
        var state by mutableStateOf(connected)
        var hosted by mutableStateOf<HostedAtlasLink.Status>(HostedAtlasLink.Status.Hosting)
        var room by mutableStateOf(LanUiState(remote = table.state.value.game, role = "table", online = true))
        var planesMessage by mutableStateOf<String?>(null)
        val model = LanViewModel(context, table)
        ActivityScenario.launch(MainActivity::class.java).use { scenario ->
            scenario.onActivity { activity ->
                activity.requestedOrientation = android.content.pm.ActivityInfo.SCREEN_ORIENTATION_LOCKED
                activity.window.addFlags(android.view.WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
                activity.setTurnScreenOn(true)
                activity.setShowWhenLocked(true)
                activity.setContent {
                com.turnhub.android.ui.theme.TurnHubTheme {
                    CompositionLocalProvider(LocalDensity provides Density(context.resources.displayMetrics.density, 1.3f)) {
                        HomeScreen(state, {}, {}, {}, {}, { _,_ -> }, {}, {}, reduceMotion = true,
                            localState = table.state.value, localTable = table,
                            onHostOnAtlas = {}, hostedStatus = hosted, hostedPlanesMessage = planesMessage,
                            sharedRoomFeedback = { RoomFeedback(room, model) },
                            localGameContent = { _,_,_ -> StandaloneLobby(table.state.value, table, embedded = true) })
                    }
                }
            } }
            fun nodes(n: AccessibilityNodeInfo?): List<AccessibilityNodeInfo> {
                if (n == null) return emptyList()
                n.refresh()
                return listOf(n) + (0 until n.childCount).flatMap { nodes(n.getChild(it)) }
            }
            fun snapshot(): List<AccessibilityNodeInfo> {
                if (android.os.Build.VERSION.SDK_INT >= 37) instrumentation.uiAutomation.clearCache()
                return nodes(instrumentation.uiAutomation.rootInActiveWindow)
            }
            fun waitFor(text: String): List<AccessibilityNodeInfo> {
                val until = SystemClock.uptimeMillis()+6000
                var all = snapshot()
                while (all.none { it.text?.toString()?.equals(text,true) == true } && SystemClock.uptimeMillis()<until) {
                    SystemClock.sleep(150); all=snapshot()
                }
                val capture = java.io.File(context.getExternalFilesDir(null), "atlas-retry-$text.png")
                capture.outputStream().use { instrumentation.uiAutomation.takeScreenshot().compress(android.graphics.Bitmap.CompressFormat.PNG,100,it) }
                assertTrue("Missing $text: " + all.mapNotNull { it.text }.joinToString(" / "), all.any { it.text?.toString()?.equals(text,true) == true }); return all
            }
            fun position(all: List<AccessibilityNodeInfo>, label: String) = Rect().also { rect ->
                all.first { it.text?.toString()?.equals(label,true) == true }.getBoundsInScreen(rect)
            }
            val initial = waitFor("1. GAME")
            val gameRect = position(initial,"1. GAME")
            val batteryRect = position(initial,"100%")
            for (phase in listOf("Retrying", "Reconnecting", "Connected")) {
                scenario.onActivity {
                    state = when (phase) {
                        "Retrying" -> connected.copy(errorMessage = "Temporary timeout")
                        "Reconnecting" -> connected.copy(offlineSinceMs = 1, errorMessage = "Connection lost")
                        else -> connected
                    }
                    hosted = if (phase == "Connected") HostedAtlasLink.Status.Hosting else HostedAtlasLink.Status.Failed("Temporary timeout")
                    room = if (phase == "Retrying") room.copy(uncertain = true, message = "The host has not confirmed this action.") else room.copy(uncertain = false, message = null)
                    planesMessage = if (phase == "Connected") "Plane artwork transfer complete" else null
                }
                val changed = waitFor("Atlas " + phase.lowercase())
                assertEquals("Game moved during $phase",gameRect,position(changed,"1. GAME"))
                assertEquals("Battery moved during $phase",batteryRect,position(changed,"100%"))
                if (phase == "Retrying") waitFor("Retry last action")
            }
            val file=java.io.File(context.getExternalFilesDir(null),"atlas-retry-layout.png")
            file.outputStream().use { instrumentation.uiAutomation.takeScreenshot().compress(android.graphics.Bitmap.CompressFormat.PNG,100,it) }
        }
    }
}
