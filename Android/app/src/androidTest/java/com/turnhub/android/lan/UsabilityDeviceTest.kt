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
        error("Missing $label; visible labels: " + nodes(instrumentation.uiAutomation.rootInActiveWindow).mapNotNull { it.text?.toString() }.take(35))
    }
    private fun click(label: String) {
        instrumentation.waitForIdleSync()
        SystemClock.sleep(150)
        var target: AccessibilityNodeInfo? = find(label)
        while (target != null && !target.isClickable) target = target.parent
        target?.performAction(AccessibilityNodeInfo.AccessibilityAction.ACTION_SHOW_ON_SCREEN.id)
        assertTrue("Can't click $label", target?.performAction(AccessibilityNodeInfo.ACTION_CLICK) == true)
        instrumentation.waitForIdleSync()
        SystemClock.sleep(150)
    }
    private fun clickScrolled(label: String) {
        repeat(20) {
            val all = nodes(instrumentation.uiAutomation.rootInActiveWindow)
            if (all.any { it.text?.toString() == label || it.contentDescription?.toString() == label }) { click(label); return }
            all.firstOrNull { it.isScrollable }?.performAction(AccessibilityNodeInfo.ACTION_SCROLL_FORWARD)
            SystemClock.sleep(150)
        }
        click(label)
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
            click("Add guest")
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
    @Test fun choosingANewStarterAndThenRandomDoesNotCancelTheCountdown() {
        val table = StandaloneTable(MemoryStandaloneStore(StandaloneGame(), LocalLibrary())).apply {
            addGuest("Starter One"); addGuest("Starter Two"); chooseStarter(0)
        }
        ActivityScenario.launch(MainActivity::class.java).use { scenario ->
            scenario.onActivity { activity -> prepare(activity); activity.setContent { TurnHubTheme {
                val state by table.state.collectAsState()
                if (state.game.state == com.turnhub.android.protocol.TableState.LOBBY) StandaloneLobby(state, table, embedded = true)
                else Text("Game is running")
            } } }
            clickScrolled("Start game")
            click("Starter Two")
            find("Get ready")
            find("Game is running")
            assertEquals(1, table.state.value.game.active)
            scenario.onActivity {
                kotlinx.coroutines.runBlocking { table.control(com.turnhub.android.data.TabletSeat(StandaloneGame.seatHandle(0).id, 1), "reset") }
                table.chooseStarter(0)
            }
            clickScrolled("Start game"); click("Random")
            find("Get ready"); find("Game is running")
            assertEquals(com.turnhub.android.protocol.TableState.RUNNING, table.state.value.game.state)
            assertTrue(table.state.value.game.starter in table.state.value.game.players.indices)
        }
    }
    @Test fun editsDuringTheStartCountdownStillCancelIt() {
        val table = StandaloneTable(MemoryStandaloneStore(StandaloneGame(), LocalLibrary())).apply {
            addGuest("Starter One"); addGuest("Starter Two")
        }
        ActivityScenario.launch(MainActivity::class.java).use { scenario ->
            scenario.onActivity { activity -> prepare(activity); activity.setContent { TurnHubTheme {
                val state by table.state.collectAsState()
                StandaloneLobby(state, table, embedded = true)
            } } }
            clickScrolled("Start game"); click("Starter Two"); find("Get ready")
            val before = table.state.value.game.revision
            scenario.onActivity { table.addGuest("Late arrival") }
            assertTrue(table.state.value.game.revision > before)
            SystemClock.sleep(3_500)
            assertEquals(com.turnhub.android.protocol.TableState.LOBBY, table.state.value.game.state)
        }
    }
    @Test fun savedSetupsUseNamedActionsAndWaitBeforeReplacingOrDeleting() {
        val actions = mutableListOf<Pair<String, String>>()
        ActivityScenario.launch(MainActivity::class.java).use { scenario ->
            scenario.onActivity { activity -> prepare(activity); activity.setContent { TurnHubTheme {
                Column {
                    SavedSetupActions(listOf("Friday"), true, true, "Stored on the host device.",
                        summaries = mapOf("Friday" to "Commander · One, Two")) { op, name -> actions += op to name }
                }
            } } }
            click("Saved game setups · 1")
            find("Commander · One, Two")
            click("Load setup"); find("Load setup: Friday?"); click("Cancel")
            assertTrue(actions.isEmpty())
            click("Save current setup")
            val input = nodes(instrumentation.uiAutomation.rootInActiveWindow).first { it.className == "android.widget.EditText" }
            assertTrue(input.performAction(AccessibilityNodeInfo.ACTION_SET_TEXT, android.os.Bundle().apply {
                putCharSequence(AccessibilityNodeInfo.ACTION_ARGUMENT_SET_TEXT_CHARSEQUENCE, "Friday")
            }))
            click("Save setup"); find("Replace setup: Friday?"); click("Cancel")
            assertTrue(actions.isEmpty())
            click("Delete setup"); find("Delete setup: Friday?"); click("Cancel")
            assertTrue(actions.isEmpty())
            click("Load setup"); click("Load setup")
            assertEquals(listOf("load" to "Friday"), actions)
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
            click("I won the game")
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
