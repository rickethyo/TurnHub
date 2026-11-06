package com.turnhub.android.lan

import android.content.Context
import android.content.ContextWrapper
import android.os.SystemClock
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.turnhub.android.standalone.*
import kotlinx.coroutines.*
import org.json.*
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith
import java.net.InetSocketAddress
import java.util.UUID
import java.util.concurrent.atomic.AtomicBoolean
import java.util.concurrent.atomic.AtomicInteger

/** Isolated games and reconnect stores; never edits the owner's game or account data. */
@RunWith(AndroidJUnit4::class)
class AtlasRetryDeviceTest {
    private val instrumentation = InstrumentationRegistry.getInstrumentation()
    private val app get() = instrumentation.targetContext
    private class IsolatedContext(base: Context) : ContextWrapper(base) {
        private val prefix = "retry-test-" + UUID.randomUUID().toString()
        private val opened = mutableSetOf<String>()
        override fun getApplicationContext(): Context = this
        override fun getSharedPreferences(name: String, mode: Int): android.content.SharedPreferences {
            val separate = "$prefix-$name"; opened += separate
            return baseContext.getSharedPreferences(separate, mode)
        }
        fun clear() { opened.forEach { baseContext.getSharedPreferences(it, 0).edit().clear().commit() } }
    }
    private fun main(action: () -> Unit) = instrumentation.runOnMainSync(action)
    private fun waitFor(message: String, condition: () -> Boolean) {
        val deadline = SystemClock.uptimeMillis() + 15_000
        while (!condition() && SystemClock.uptimeMillis() < deadline) SystemClock.sleep(100)
        assertTrue(message, condition())
    }
    private class Peer(context: Context) : AutoCloseable {
        val isolated = IsolatedContext(context)
        val table = StandaloneTable(MemoryStandaloneStore(StandaloneGame(), LocalLibrary())).apply {
            addPlayer("Retry One"); addPlayer("Retry Two")
        }
        val guest = LanViewModel(isolated, StandaloneTable(MemoryStandaloneStore(StandaloneGame(), LocalLibrary())))
        val badPolls = AtomicInteger()
        val polls = AtomicInteger()
        val loseCommandReply = AtomicBoolean()
        private val session = "a".repeat(32)
        private val key = "b".repeat(64)
        private fun base(op: String) = JSONObject().put("v", LAN_VERSION).put("session", session).put("op", op)
        private fun core() = LanSessions(session, key, { table.state.value.game }, table::applyLanAction)
        private val room = LanRoom(JSONObject().put("session", session).put("key", key), save = { true },
            snapshot = { core().handle(base("preview").put("key", key)) },
            seats = { table.state.value.game.players.mapIndexed { i,p -> LanRoom.RoomSeat(p.localId ?: "guest:$i", i+1, p.name) } },
            profiles = { table.roomProfiles() }, attach = { table.state.value.game.players.first().localId },
            command = { _,_ -> JSONObject().put("code", "forbidden") }, setup = table::applyRoomSetup)
        private val tls = lanServerContext()
        private val socket = tls.first.serverSocketFactory.createServerSocket().apply { bind(InetSocketAddress("0.0.0.0", 0)) }
        private val server = LanServer(socket) { request -> runBlocking {
            if (request.optString("op") == "poll") {
                polls.incrementAndGet()
                if (badPolls.getAndUpdate { maxOf(0, it-1) } > 0) return@runBlocking base("poll").put("code", "unavailable")
            }
            val answer = room.handle(request)
            if (request.optString("op") == "command" && loseCommandReply.getAndSet(false)) base("command").put("code", "unavailable") else answer
        } }
        val invitation = LanInvite(currentLanWifi(context).address.hostAddress!!, server.port, tls.second, session, key)
        override fun close() {
            InstrumentationRegistry.getInstrumentation().runOnMainSync { guest.leave() }
            server.close(); isolated.clear()
        }
    }
    private fun join(peer: Peer) {
        main { peer.guest.preview(peer.invitation.encode()) }
        waitFor("Room preview failed") { peer.guest.state.value.preview != null }
        main { peer.guest.join("table", 0) }
        waitFor("Room join failed") { peer.guest.state.value.online && !peer.guest.state.value.busy }
    }

    @Test fun shortPollFailuresStayQuietAndConfirmedOutagesRecover() {
        Peer(app).use { peer ->
            join(peer)
            val before = peer.guest.state.value.remote
            val scope = CoroutineScope(SupervisorJob() + Dispatchers.Main.immediate)
            val wentOffline = AtomicBoolean()
            val observer = scope.launch { peer.guest.state.collect { if (!it.online) wentOffline.set(true) } }
            try {
                peer.badPolls.set(2)
                peer.table.setTurnTimer(60_000)
                waitFor("Missing recovery poll") { peer.guest.state.value.remote?.turnTimerMs == 60_000L }
                assertFalse("A short poll interruption must not replace the game", wentOffline.get())
                assertNull(peer.guest.state.value.message)
                assertEquals(before!!.gameId, peer.guest.state.value.remote!!.gameId)
                peer.badPolls.set(3)
                waitFor("A persistent outage must become visible") { !peer.guest.state.value.online }
                assertNotNull(peer.guest.state.value.message)
                assertEquals(before.gameId, peer.guest.state.value.remote!!.gameId)
                waitFor("Recovery should clear the poll error") { peer.guest.state.value.online && peer.guest.state.value.message == null }
            } finally { observer.cancel(); scope.cancel() }
        }
    }

    @Test fun anUnconfirmedSetupKeepsItsReceiptAndRetryOnlyAppliesItOnce() {
        Peer(app).use { peer ->
            join(peer)
            peer.loseCommandReply.set(true)
            main { peer.guest.setup(peer.guest.state.value.remote!!, "guest") }
            waitFor("The lost reply must remain unconfirmed") { !peer.guest.state.value.busy && peer.guest.state.value.uncertain }
            waitFor("The poll must recover without clearing the pending command") { peer.guest.state.value.online && peer.guest.state.value.remote!!.players.size == 3 }
            assertTrue(peer.guest.state.value.uncertain)
            assertNotNull(peer.guest.state.value.message)
            main { peer.guest.retry() }
            waitFor("Receipt retry failed") { !peer.guest.state.value.busy && !peer.guest.state.value.uncertain }
            assertEquals(3, peer.table.state.value.game.players.size)
            assertEquals(3, peer.guest.state.value.remote!!.players.size)
            assertNull(peer.guest.state.value.message)
        }
    }

    @Test fun slowAtlasAuthorizationCoalescesChecksAndCachesFromTheReply() = runBlocking {
        val context = IsolatedContext(app)
        val host = LanViewModel(context, StandaloneTable(MemoryStandaloneStore(StandaloneGame(), LocalLibrary())))
        val checks = AtomicInteger()
        val revoked = AtomicBoolean()
        host.atlasCall = { _,_,fields ->
            if (fields.first().first == "phone") {
                checks.incrementAndGet()
                if (revoked.get()) com.turnhub.android.data.RawResponse(403, "{}") else {
                    delay(5_100)
                    com.turnhub.android.data.RawResponse(200, """{"phone":"ABCDEF01"}""")
                }
            } else com.turnhub.android.data.RawResponse(200, """{"phone":"ABCDEF01"}""")
        }
        try {
            val offered = host.publishedInvitation() ?: error("Host did not start")
            val invitation = LanInvite.parse(offered).copy(key = "c".repeat(64))
            fun base(op: String) = JSONObject().put("v", LAN_VERSION).put("session", invitation.session).put("op", op)
            val joined = withContext(Dispatchers.IO) { lanExchange(invitation, base("join").put("key", invitation.key)
                .put("role", "table").put("joinId", UUID.randomUUID().toString())) }
            assertEquals("ok", joined.getString("code"))
            fun poll() = base("poll").put("token", joined.getString("token"))
            val replies = coroutineScope { (1..3).map { async(Dispatchers.IO) { runCatching { lanExchange(invitation, poll()) }.getOrNull() } }.awaitAll() }
            assertTrue("The delayed check should outlive the clients' read timeout", replies.all { it == null })
            delay(1_400)
            assertEquals(1, checks.get())
            val recovered = withContext(Dispatchers.IO) { lanExchange(invitation, poll()) }
            assertEquals("ok", recovered.getString("code"))
            assertEquals("The cache starts when verification finishes", 1, checks.get())
            revoked.set(true)
            delay(5_100)
            val refused = withContext(Dispatchers.IO) { lanExchange(invitation, poll()) }
            assertEquals("unauthorized", refused.getString("code"))
        } finally { main { host.stopHosting() }; context.clear() }
    }
    @Test fun liveAtlasReadOnlySampling() = runBlocking {
        org.junit.Assume.assumeTrue("Explicit live-device opt-in", InstrumentationRegistry.getArguments().getString("atlasProbe") == "true")
        val prefs = app.getSharedPreferences("atlas_wifi", 0)
        val address = prefs.getString("home_address", null) ?: error("No saved home Atlas")
        val pin = prefs.getString("home_pin", null) ?: error("No saved TLS pin")
        val endpoint = com.turnhub.android.data.AtlasEndpoint.parse("https://$address:${prefs.getInt("home_port",443)}").getOrThrow().withTlsPin(pin)
        val transport = com.turnhub.android.data.HttpAtlasTransport(endpoint,
            baseOpener = com.turnhub.android.data.WifiPreferringConnectionOpener(app))
        var successes = 0
        val boots = mutableSetOf<String>()
        repeat(6) { sample ->
            val begin = SystemClock.elapsedRealtime()
            try {
                val state = transport.getState()
                successes++; boots += state.bootId
                android.util.Log.i("TurnHubRetryProbe", "sample=$sample ok elapsedMs=${SystemClock.elapsedRealtime()-begin} boot=${state.bootId}")
            } catch (e: Exception) {
                android.util.Log.i("TurnHubRetryProbe", "sample=$sample failed elapsedMs=${SystemClock.elapsedRealtime()-begin} type=${e.javaClass.simpleName}")
            }
            delay(1000)
        }
        android.util.Log.i("TurnHubRetryProbe", "successes=$successes/6 boots=${boots.size}")
        assertTrue("Atlas must answer reliably under the live phone workload", successes >= 4)
        assertEquals("Atlas restarted during the read-only sample", 1, boots.size)
    }

}
