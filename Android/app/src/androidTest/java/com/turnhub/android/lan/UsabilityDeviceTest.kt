package com.turnhub.android.lan

import android.os.SystemClock
import android.view.accessibility.AccessibilityNodeInfo
import androidx.activity.compose.setContent
import androidx.compose.foundation.layout.*
import androidx.compose.material3.Text
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.test.core.app.ActivityScenario
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.turnhub.android.MainActivity
import com.turnhub.android.data.AtlasLinkHoldService
import com.turnhub.android.standalone.*
import com.turnhub.android.ui.components.*
import com.turnhub.android.ui.home.*
import com.turnhub.android.ui.tablet.*
import com.turnhub.android.ui.theme.TurnHubTheme
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith

/** Memory tables and UI-only callbacks; no changes to real profiles or history. */
@RunWith(AndroidJUnit4::class)
class UsabilityDeviceTest {
    private val instrumentation = InstrumentationRegistry.getInstrumentation()
    private fun prepare(activity: MainActivity) {
        activity.setTurnScreenOn(true)
        activity.setShowWhenLocked(true)
        activity.window.addFlags(android.view.WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
    }
    private fun nodes(node: AccessibilityNodeInfo?): List<AccessibilityNodeInfo> {
        if (node == null) return emptyList()
        node.refresh()
        return listOf(node) + (0 until node.childCount).flatMap { nodes(node.getChild(it)) }
    }
    private fun find(label: String): AccessibilityNodeInfo {
        val deadline = SystemClock.uptimeMillis() + 6_000
        do {
            if (android.os.Build.VERSION.SDK_INT >= 37) instrumentation.uiAutomation.clearCache()
            nodes(instrumentation.uiAutomation.rootInActiveWindow).firstOrNull {
                it.text?.toString() == label || it.contentDescription?.toString() == label
            }?.let { return it }
            SystemClock.sleep(100)
        } while (SystemClock.uptimeMillis() < deadline)
        error("Missing $label")
    }
    private fun click(label: String) {
        var target: AccessibilityNodeInfo? = find(label)
        while (target != null && !target.isClickable) target = target.parent
        target?.performAction(AccessibilityNodeInfo.AccessibilityAction.ACTION_SHOW_ON_SCREEN.id)
        assertTrue("Can't click $label", target?.performAction(AccessibilityNodeInfo.ACTION_CLICK) == true)
    }
    @Test fun addingAGuestRevealsTheNewSeat() {
        val table = StandaloneTable(MemoryStandaloneStore(StandaloneGame(), LocalLibrary())).apply {
            addGuest("Review One"); addGuest("Review Two")
        }
        ActivityScenario.launch(MainActivity::class.java).use { scenario ->
            scenario.onActivity { activity -> prepare(activity); activity.setContent { TurnHubTheme {
                val state by table.state.collectAsState()
                StandaloneLobby(state, table, embedded = true)
            } } }
            click("Add Guest")
            instrumentation.waitForIdleSync()
            assertEquals(3, table.state.value.game.players.size)
            val guest = find(table.state.value.game.players.last().name)
            val bounds = android.graphics.Rect().also(guest::getBoundsInScreen)
            assertTrue("New guest must be visible", guest.isVisibleToUser)
            assertTrue("New guest must be inside the screen", bounds.top >= 0 && bounds.bottom <= instrumentation.targetContext.resources.displayMetrics.heightPixels)
        }
    }
    @Test fun joiningIsAvailableWithoutAtlasOrAnExistingRoom() {
        var opened = false
        val table = StandaloneTable(MemoryStandaloneStore(StandaloneGame(), LocalLibrary()))
        ActivityScenario.launch(MainActivity::class.java).use { scenario ->
            scenario.onActivity { activity -> prepare(activity); activity.setContent { TurnHubTheme {
                HomeScreen(HomeUiState(), {}, {}, {}, {}, { _, _ -> }, {}, {}, reduceMotion = true,
                    localState = table.state.value, localTable = table, onOpenSharedGame = { opened = true },
                    localGameContent = { _,_,_ -> Text("Local setup") })
            } } }
            click("Play together · Join or host a table")
            instrumentation.waitForIdleSync()
            assertTrue(opened)
        }
    }
    @Test fun confirmationWaitsForTheReaderAndPassOffersATapAlternative() {
        var wins = 0
        var passes = 0
        ActivityScenario.launch(MainActivity::class.java).use { scenario ->
            scenario.onActivity { activity -> prepare(activity); activity.setContent { TurnHubTheme {
                Column {
                    ArmedButton("I won the game", "Record your win and finish this game.", Tone.GOOD) { wins++ }
                    HoldToPass(false, { passes++ }, rememberHaptics(), Modifier.fillMaxWidth(), compact = true)
                }
            } } }
            click("I won the game")
            find("Record your win and finish this game.")
            SystemClock.sleep(4_200)
            find("Record your win and finish this game.")
            assertEquals(0, wins)
            click("Cancel")
            click("I won the game")
            click("Confirm")
            instrumentation.waitForIdleSync()
            assertEquals(1, wins)
            find("Hold to pass")
            click("Pass the turn")
            find("Pass the turn?")
            assertEquals(0, passes)
            click("Pass turn")
            instrumentation.waitForIdleSync()
            assertEquals(1, passes)
        }
    }
    @Test fun anExistingHostCanChooseAnotherTableWithoutLosingItsLocalSetup() {
        val app = instrumentation.targetContext
        val names = mutableSetOf<String>()
        val prefix = "usability-switch-${java.util.UUID.randomUUID()}-"
        val isolated = object : android.content.ContextWrapper(app) {
            override fun getApplicationContext(): android.content.Context = this
            override fun getSharedPreferences(name: String, mode: Int): android.content.SharedPreferences {
                names += prefix + name
                return app.getSharedPreferences(prefix + name, mode)
            }
        }
        val table = StandaloneTable(MemoryStandaloneStore(StandaloneGame(), LocalLibrary())).apply { addGuest("Test guest") }
        val original = table.state.value.game
        val model = LanViewModel(isolated, table)
        var stoppedAtlas = false
        try {
            ActivityScenario.launch(MainActivity::class.java).use { scenario ->
                scenario.onActivity { activity ->
                    prepare(activity)
                    model.startHosting(true)
                    activity.setContent { TurnHubTheme {
                        val state by model.state.collectAsState()
                        SharedGameScreen(state, table.state.value.game, model, { it() }, true, {},
                            onStopShowingOnAtlas = { stoppedAtlas = true })
                    } }
                }
                click("Join another table")
                find("Stop hosting and join another table?")
                assertFalse(stoppedAtlas)
                assertNotNull(model.state.value.host)
                click("Stop hosting")
                find("Host or join a game")
                assertTrue(stoppedAtlas)
                assertNull(model.state.value.host)
                assertEquals(original, table.state.value.game)
            }
        } finally {
            instrumentation.runOnMainSync { model.stopHosting() }
            names.forEach { app.deleteSharedPreferences(it) }
        }
    }
    @Test fun quickConnectionHoldAndReleaseDoesNotCancelTheForegroundStart() {
        val context = instrumentation.targetContext
        ActivityScenario.launch(MainActivity::class.java).use { scenario ->
            scenario.onActivity(::prepare)
            repeat(20) {
                scenario.onActivity { AtlasLinkHoldService.hold(it); AtlasLinkHoldService.release(it) }
            }
            // The old lifecycle race crashes the process after Android's start deadline.
            SystemClock.sleep(10_000)
            assertFalse(AtlasLinkHoldService.active)
            scenario.onActivity { AtlasLinkHoldService.hold(it) }
            val deadline = SystemClock.uptimeMillis() + 3_000
            while (!AtlasLinkHoldService.active && SystemClock.uptimeMillis() < deadline) SystemClock.sleep(50)
            assertTrue(AtlasLinkHoldService.active)
            scenario.onActivity { AtlasLinkHoldService.release(it) }
            val released = SystemClock.uptimeMillis() + 3_000
            while (AtlasLinkHoldService.active && SystemClock.uptimeMillis() < released) SystemClock.sleep(50)
            assertFalse(AtlasLinkHoldService.active)
        }
    }
}
