package com.turnhub.android.lan

import androidx.activity.compose.setContent
import android.media.AudioManager
import android.media.MediaPlayer
import android.os.SystemClock
import android.util.Log
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.turnhub.android.R
import com.turnhub.android.data.GameSoundPlayer
import com.turnhub.android.domain.GameSoundCue
import com.turnhub.android.protocol.TableState
import com.turnhub.android.standalone.*
import kotlinx.coroutines.runBlocking
import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith
import java.net.InetSocketAddress
import java.util.UUID

/** Real Android TLS, room membership, JNI and durable records, isolated from the user's table. */
@RunWith(AndroidJUnit4::class)
class PlaytestDeviceTest {
    @Test fun remoteCommanderRematchAndResetAcrossPinnedTls() {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        val prefs = context.getSharedPreferences("playtest_regression", 0)
        assertTrue(prefs.edit().clear().commit())
        val store = object : StandaloneStore {
            override fun loadGame() = prefs.getString("game", null)
            override fun loadLibrary() = prefs.getString("library", null)
            override fun loadRecords(): String? = null
            override fun saveSnapshot(game: String, library: String) = prefs.edit().putString("game", game).putString("library", library).commit()
        }
        val table = StandaloneTable(store, shortGameMs = 0)
        table.addPlayer("Test host"); table.addGuest("Test guest")
        val identity = table.state.value.library.players.single().profileId
        fun base(op: String) = JSONObject().put("v", LAN_VERSION).put("session", "test-room").put("op", op)
        fun core() = LanSessions("test-room", "test-key", { table.state.value.game }, table::applyLanAction)
        val room = LanRoom(JSONObject().put("session", "test-room").put("key", "test-key"), save = { true },
            snapshot = { core().handle(base("preview").put("key", "test-key")) },
            seats = { listOf(LanRoom.RoomSeat(identity, 1, "Test host"), LanRoom.RoomSeat("guest", 2, "Test guest")) },
            profiles = { JSONArray().put(JSONObject().put("profileId", identity).put("name", "Test host")) },
            attach = { identity }, command = { request, target ->
                val local = core()
                val joined = local.handle(base("join").put("key", "test-key").put("role", "player")
                    .put("player", target).put("joinId", UUID.randomUUID().toString()))
                local.handle(JSONObject(request.toString()).put("token", joined.getString("token")))
            })
        val (tls, pin) = lanServerContext()
        val socket = tls.serverSocketFactory.createServerSocket().apply { bind(InetSocketAddress("127.0.0.1", 0)) }
        val server = LanServer(socket) { request -> runBlocking { room.handle(request) } }
        try {
            val invitation = LanInvite("127.0.0.1", server.port, pin, "test-room", "test-key")
            fun exchange(r: JSONObject) = lanExchange(invitation, r)
            val joined = exchange(base("join").put("key", "test-key").put("role", "player")
                .put("profileId", identity).put("joinId", UUID.randomUUID().toString()))
            assertEquals("ok", joined.getString("code"))
            fun action(kind: String) = base("command").put("token", joined.getString("token"))
                .put("epoch", joined.getString("epoch")).put("actionId", UUID.randomUUID().toString())
                .put("match", table.state.value.game.gameId).put("revision", table.state.value.game.revision)
                .put("kind", kind).put("value", 0).put("slot", 0).put("source", 0).put("target", 2)
            // A forged target cannot rename another player's commander, even in setup.
            assertEquals("ok", exchange(action("set-commander").put("card", LanCard("Atraxa", null, false).json())).getString("code"))
            assertEquals("Atraxa", table.state.value.game.players[0].commander)
            assertNull(table.state.value.game.players[1].commander)
            table.start()
            assertEquals("ok", exchange(action("eliminate")).getString("code"))
            assertEquals(TableState.GAME_OVER, table.state.value.game.state)
            val rematch = action("rematch")
            assertEquals("ok", exchange(rematch).getString("code"))
            val match = table.state.value.game.gameId
            assertEquals("ok", exchange(rematch).getString("code"))
            assertEquals(match, table.state.value.game.gameId)
            assertEquals("ok", exchange(action("eliminate")).getString("code"))
            assertEquals("ok", exchange(action("reset")).getString("code"))
            assertEquals(TableState.LOBBY, table.state.value.game.state)
            assertEquals(2, StandaloneTable(store).state.value.library.history.size)
        } finally { server.close(); prefs.edit().clear().commit() }
    }

    @Test fun joinedLobbyEditsTravelOverPinnedTlsAndPersist() {
        val store = MemoryStandaloneStore(StandaloneGame(), LocalLibrary())
        val table = StandaloneTable(store)
        table.addPlayer("Setup One"); table.addPlayer("Setup Two")
        fun base(op: String) = JSONObject().put("v",LAN_VERSION).put("session","setup-wire").put("op",op)
        fun core() = LanSessions("setup-wire","setup-key",{ table.state.value.game },table::applyLanAction,
            profileIds = { val state = table.state.value; state.game.players.map { p -> state.library.players.firstOrNull { it.localId == p.localId }?.profileId } })
        val room = LanRoom(JSONObject().put("session","setup-wire").put("key","setup-key"),save = { true },
            snapshot = { core().handle(base("preview").put("key","setup-key")) },
            seats = { table.state.value.game.players.mapIndexed { index,p -> LanRoom.RoomSeat(p.localId ?: "guest:$index",index+1,p.name) } },
            profiles = { table.roomProfiles() }, attach = { table.state.value.game.players.first().localId },
            command = { _,_ -> JSONObject().put("code","forbidden") },setup = table::applyRoomSetup)
        val (tls,pin) = lanServerContext()
        val socket = tls.serverSocketFactory.createServerSocket().apply { bind(InetSocketAddress("127.0.0.1",0)) }
        val server = LanServer(socket) { request -> runBlocking { room.handle(request) } }
        try {
            val invitation = LanInvite("127.0.0.1",server.port,pin,"setup-wire","setup-key")
            val joined = lanExchange(invitation,base("join").put("key","setup-key").put("role","player").put("joinId",UUID.randomUUID().toString()))
            assertEquals("ok",joined.getString("code"))
            fun command(op:String,vararg fields:Pair<String,Any>):JSONObject {
                val data = JSONObject().put("op",op); fields.forEach { (k,v) -> data.put(k,v) }
                return base("command").put("token",joined.getString("token")).put("epoch",joined.getString("epoch"))
                    .put("actionId",UUID.randomUUID().toString()).put("match",table.state.value.game.gameId)
                    .put("revision",table.state.value.game.revision).put("kind","setup").put("value",0).put("slot",0).put("source",0).put("setup",data)
            }
            val identities = table.state.value.game.players.map { it.localId }
            val swap = command("swap","a" to 0,"b" to 1)
            assertEquals("ok",lanExchange(invitation,swap).getString("code"))
            assertEquals("ok",lanExchange(invitation,swap).getString("code"))
            assertEquals(identities.reversed(),table.state.value.game.players.map { it.localId })
            assertEquals("ok",lanExchange(invitation,command("timer","ms" to 60_000L)).getString("code"))
            assertEquals("ok",lanExchange(invitation,command("start","index" to 1)).getString("code"))
            val restored = StandaloneTable(store).state.value.game
            assertEquals(TableState.RUNNING,restored.state)
            assertEquals(60_000L,restored.turnTimerMs)
            assertEquals(identities.reversed(),restored.players.map { it.localId })
            assertEquals("rejected",lanExchange(invitation,command("clear")).getString("code"))
        } finally { server.close() }
    }

    @Test fun joinedViewModelRefreshesHostSetupOptions() {
        val instrumentation = InstrumentationRegistry.getInstrumentation()
        val app = instrumentation.targetContext
        val prefix = "game_tab_wire_" + UUID.randomUUID().toString()
        val opened = mutableSetOf<String>()
        val isolated = object : android.content.ContextWrapper(app) {
            override fun getApplicationContext(): android.content.Context = this
            override fun getSharedPreferences(name: String, mode: Int): android.content.SharedPreferences {
                val separate = "$prefix-$name"; opened += separate
                return app.getSharedPreferences(separate, mode)
            }
        }
        val table = StandaloneTable(MemoryStandaloneStore(StandaloneGame(), LocalLibrary()))
        table.addPlayer("ViewModel One"); table.addPlayer("ViewModel Two")
        val session = "a".repeat(32); val key = "b".repeat(64)
        fun base(op: String) = JSONObject().put("v",LAN_VERSION).put("session",session).put("op",op)
        fun core() = LanSessions(session,key,{ table.state.value.game },table::applyLanAction)
        val room = LanRoom(JSONObject().put("session",session).put("key",key),save = { true },
            snapshot = { core().handle(base("preview").put("key",key)) },
            seats = { table.state.value.game.players.mapIndexed { index,p -> LanRoom.RoomSeat(p.localId ?: "guest:$index",index+1,p.name) } },
            profiles = { table.roomProfiles() }, attach = { table.state.value.game.players.first().localId },
            command = { _,_ -> JSONObject().put("code","forbidden") },setup = table::applyRoomSetup,
            setupOptions = { JSONObject().put("names",JSONArray(table.state.value.library.gameSetups.map { it.name }))
                .put("guests",JSONArray(table.state.value.library.guestNames)) })
        val (tls,pin) = lanServerContext()
        val socket = tls.serverSocketFactory.createServerSocket().apply { bind(InetSocketAddress("0.0.0.0",0)) }
        val server = LanServer(socket) { request -> runBlocking { room.handle(request) } }
        val guest = LanViewModel(isolated, StandaloneTable(MemoryStandaloneStore(StandaloneGame(),LocalLibrary())))
        fun waitFor(condition: () -> Boolean) {
            val deadline = SystemClock.uptimeMillis()+12_000
            while (!condition() && SystemClock.uptimeMillis()<deadline) SystemClock.sleep(100)
            assertTrue(guest.state.value.message ?: "Guest state timed out",condition())
        }
        try {
            val invite = LanInvite(currentLanWifi(app).address.hostAddress!!,server.port,pin,session,key)
            instrumentation.runOnMainSync { guest.preview(invite.encode()) }
            waitFor { guest.state.value.preview != null }
            instrumentation.runOnMainSync { guest.join("player",0,profileId = table.state.value.library.players.first().profileId) }
            waitFor { guest.state.value.remote != null && !guest.state.value.busy }
            instrumentation.runOnMainSync { guest.setup(guest.state.value.remote!!,"save-setup","name" to "Friday") }
            waitFor { "Friday" in guest.state.value.setupNames && !guest.state.value.busy }
            instrumentation.runOnMainSync { guest.setup(guest.state.value.remote!!,"guest") }
            waitFor { "Guest 1" in guest.state.value.guestNames && !guest.state.value.busy }
            assertEquals(3,guest.state.value.remote!!.players.size)
            assertEquals(2,table.state.value.library.players.size)
        } finally {
            instrumentation.runOnMainSync { guest.leave() }
            server.close()
            opened.forEach { app.getSharedPreferences(it,0).edit().clear().commit() }
        }
    }

    @Test fun everyDeviceSoundDecodesAndPlaybackUsesDevicePolicy() {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        val resources = listOf(R.raw.game_start, R.raw.game_pass, R.raw.game_pause, R.raw.game_resume, R.raw.game_win, R.raw.game_draw)
        resources.forEach { resource ->
            val decoder = requireNotNull(MediaPlayer.create(context, resource))
            try { assertTrue(decoder.duration > 0) } finally { decoder.release() }
        }
        val manager = context.getSystemService(AudioManager::class.java)
        Log.i("TurnHubPlaytest", "Sound check: ringer=${manager.ringerMode}, media=${manager.getStreamVolume(AudioManager.STREAM_MUSIC)}")
        val player = GameSoundPlayer(context)
        try {
            player.setForeground(true)
            SystemClock.sleep(800)
            GameSoundCue.entries.forEach { player.play(it, 25); SystemClock.sleep(650) }
        } finally { player.release() }
    }
    @Test fun profilePickerKeepsTheTableVisible() {
        val instrumentation = InstrumentationRegistry.getInstrumentation()
        val context = instrumentation.targetContext
        val table = StandaloneTable(MemoryStandaloneStore(StandaloneGame(), LocalLibrary()))
        val game = StandaloneGame().addPlayer("Test host").addPlayer("Test player")
            .chooseStarter(0).start(System.currentTimeMillis(), "picker-preview")
        val model = LanViewModel(context, table)
        val state = LanUiState(atlasRoom = true, preview = game,
            directory = listOf(RoomProfile("ABCDEF01", "Test host", false), RoomProfile("ABCDEF02", "Test player", false)))
        androidx.test.core.app.ActivityScenario.launch(com.turnhub.android.MainActivity::class.java).use { scenario ->
            scenario.onActivity { activity -> activity.setContent {
                com.turnhub.android.ui.theme.TurnHubTheme {
                    com.turnhub.android.ui.tablet.AtlasRoomLobby(state, model)
                }
            } }
            SystemClock.sleep(1000)
            fun text(node: android.view.accessibility.AccessibilityNodeInfo?): String = if (node == null) "" else
                node.text?.toString().orEmpty() + " " + (0 until node.childCount).joinToString { text(node.getChild(it)) }
            val end = SystemClock.uptimeMillis() + 4000
            while (!text(instrumentation.uiAutomation.rootInActiveWindow).contains("Choose your profile") && SystemClock.uptimeMillis() < end) SystemClock.sleep(150)
            val output = java.io.File(context.getExternalFilesDir(null), "playtest-picker.png")
            output.outputStream().use { instrumentation.uiAutomation.takeScreenshot().compress(android.graphics.Bitmap.CompressFormat.PNG, 100, it) }
            assertTrue(text(instrumentation.uiAutomation.rootInActiveWindow), text(instrumentation.uiAutomation.rootInActiveWindow).contains("Choose your profile"))
        }
    }

}
