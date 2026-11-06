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
