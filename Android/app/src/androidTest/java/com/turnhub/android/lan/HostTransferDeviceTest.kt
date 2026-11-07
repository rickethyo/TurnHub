package com.turnhub.android.lan

import android.content.Context
import android.content.ContextWrapper
import android.os.SystemClock
import androidx.activity.compose.setContent
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

/** Real pinned LAN sockets, isolated room preferences and memory games only. */
@RunWith(AndroidJUnit4::class)
class HostTransferDeviceTest {
    private val instrumentation = InstrumentationRegistry.getInstrumentation()
    private fun main(action: () -> Unit) = instrumentation.runOnMainSync(action)
    private fun waitFor(check: () -> Boolean) {
        val end = SystemClock.uptimeMillis() + 15_000
        while (!check() && SystemClock.uptimeMillis() < end) SystemClock.sleep(100)
        assertTrue("Timed out waiting for host transfer state", check())
    }
    @Test fun selectedControllerAcceptsHostingAndOtherDevicesRejoinItsNewRoom() {
        val app = instrumentation.targetContext
        val prefs = mutableSetOf<String>()
        val prefix = "handoff-test-${java.util.UUID.randomUUID()}-"
        fun isolated(label: String): Context = object : ContextWrapper(app) {
            override fun getApplicationContext(): Context = this
            override fun getSharedPreferences(name: String, mode: Int): android.content.SharedPreferences {
                val key = prefix + label + name; prefs += key
                return app.getSharedPreferences(key, mode)
            }
        }
        val source = StandaloneTable(MemoryStandaloneStore(StandaloneGame(), LocalLibrary())).apply {
            addGuest("Transfer One"); addGuest("Transfer Two"); start()
            runBlocking { life(TabletSeat(StandaloneGame.seatHandle(0).id, 1), 2) }
        }
        val receiver = StandaloneTable(MemoryStandaloneStore(StandaloneGame(), LocalLibrary()))
        val observer = StandaloneTable(MemoryStandaloneStore(StandaloneGame(), LocalLibrary()))
        val host = LanViewModel(isolated("host"), source)
        val target = LanViewModel(isolated("target"), receiver)
        val display = LanViewModel(isolated("display"), observer)
        var released = false; var accepted = false
        host.onHostingReleased = { released = true }
        target.onHostingReceived = { accepted = true }
        try {
            ActivityScenario.launch(MainActivity::class.java).use { scenario ->
                scenario.onActivity { activity ->
                    activity.setContent { TurnHubTheme { androidx.compose.material3.Text("Host transfer verification") } }
                    host.startHosting(false)
                }
                waitFor { host.state.value.host != null }
                val invitation = requireNotNull(host.state.value.host?.qr)
                main { target.preview(invitation); display.preview(invitation) }
                waitFor { target.state.value.preview != null && display.state.value.preview != null }
                main { target.join("table", 0); display.join("display", 0) }
                waitFor { target.state.value.remote != null && display.state.value.remote != null && host.state.value.guests.size == 2 }
                val match = source.state.value.game.gameId
                main { host.offerHost(host.state.value.guests.first { it.role == "table" }) }
                waitFor { target.state.value.hostOffer != null }
                assertNotNull(source.state.value.game.hostTransfer)
                main { target.acceptHostOffer() }
                waitFor { target.state.value.host != null && host.state.value.host == null }
                assertTrue(released); assertTrue(accepted)
                assertTrue(source.state.value.game.players.isEmpty())
                assertEquals(match, receiver.state.value.game.gameId)
                assertEquals(42, receiver.state.value.game.players.first().life)
                waitFor { !display.state.value.joined }
                main { display.preview(requireNotNull(target.state.value.host?.qr)) }
                waitFor { display.state.value.preview != null }
                main { display.join("display", 0) }
                waitFor { display.state.value.remote?.gameId == match }
                assertEquals(42, display.state.value.remote?.players?.first()?.life)
            }
        } finally {
            main { display.leave(); target.stopHosting(); host.cancelHostOffer(); host.stopHosting() }
            prefs.forEach { app.deleteSharedPreferences(it) }
        }
    }
}
