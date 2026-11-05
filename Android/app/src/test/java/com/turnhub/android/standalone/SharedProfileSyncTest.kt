package com.turnhub.android.standalone

import org.junit.Assert.*
import org.junit.Test
import kotlinx.coroutines.test.runTest

class SharedProfileSyncTest {
    private val atlas = "THA-test"
    private val day = 24 * 60 * 60 * 1000L
    private fun profile(id: String = "ABCDEF12", name: String = "Ricky") = SavedLocalPlayer("source-$id", name, id,
        credential = LocalCredential.create("1234"), lastActivityMs = 10, createdAtMs = 1)

    @Test fun `three paired devices converge on saved protected profiles without unlocking`() = runTest {
        val copies=mutableMapOf<String,org.json.JSONObject>()
        val sync=com.turnhub.android.data.AtlasPlayerCopies { method,path,fields ->
            val body=when(path) {
                "/api/player-copies" -> if(method=="GET") org.json.JSONObject().put("copies",org.json.JSONArray(copies.values)).toString()
                    else { val copy=org.json.JSONObject(fields.toMap().getValue("copy"));copies[copy.getString("profileId")]=copy;"{\"ok\":true}" }
                "/api/player-copies/stats" -> "{\"lifetime\":{\"gamesPlayed\":0,\"gamesWon\":0}}"
                "/api/player-copies/history" -> "{\"records\":[],\"more\":false}"
                else -> error(path)
            }
            com.turnhub.android.data.RawResponse(200,body)
        }
        val stores=listOf(MemoryStandaloneStore(StandaloneGame(),LocalLibrary(players=listOf(profile()))),
            MemoryStandaloneStore(StandaloneGame(),LocalLibrary(players=listOf(profile("ABCDEF13","Michael")))),
            MemoryStandaloneStore(StandaloneGame(),LocalLibrary()))
        val tables=stores.map { StandaloneTable(it) }
        tables.forEach { sync.sync(atlas,it) };tables.forEach { sync.pullDirectory(it,atlas) }
        stores.forEach { store ->
            val restored=StandaloneTable(store)
            assertEquals(setOf("Michael","Ricky"),restored.state.value.library.players.map { it.name }.toSet())
            restored.state.value.library.players.forEach { assertNotNull(it.credential);assertFalse(restored.accountUnlocked(it.localId)) }
            assertNull(restored.state.value.library.deviceAdmin)
        }
    }

    @Test fun `copy plays without PIN but settings stay locked after durable reload`() {
        val source = profile()
        val store = MemoryStandaloneStore(StandaloneGame(),LocalLibrary())
        val target = StandaloneTable(store)
        assertTrue(target.receiveSharedProfiles(atlas,listOf(SharedProfileCopy.encode(source))))
        val reloaded = StandaloneTable(store)
        val copy = reloaded.state.value.library.players.single()
        assertEquals(source.profileId,copy.profileId)
        assertEquals(source.credential,copy.credential)
        assertEquals(atlas,copy.copiedFromAtlas)
        assertFalse(reloaded.accountUnlocked(copy.localId))
        reloaded.selectPlayer(copy.localId)
        assertEquals(copy.localId,reloaded.state.value.game.players.single().localId)
        assertFalse(reloaded.accountUnlocked(copy.localId))
        assertFalse(reloaded.unlockAccount(copy.localId,"4321"))
        assertTrue(reloaded.unlockAccount(copy.localId,"1234"))
        assertNull(reloaded.state.value.library.deviceAdmin)
        assertTrue(reloaded.receiveSharedProfiles(atlas,listOf(SharedProfileCopy.encode(source))))
        assertEquals(1,reloaded.state.value.library.players.size)
    }

    @Test fun `default keeps offline copies and separate same-name identities`() {
        var now=100L
        val target=StandaloneTable(MemoryStandaloneStore(StandaloneGame(),LocalLibrary()),wallClock={now})
        assertTrue(target.receiveSharedProfiles(atlas,listOf(SharedProfileCopy.encode(profile()),SharedProfileCopy.encode(profile("ABCDEF13")))))
        target.observeCopyConnection(null);now+=10*day;target.maintainCopiedProfiles()
        assertEquals(2,target.state.value.library.players.size)
        assertTrue(target.state.value.library.keepCopiedProfiles)
    }

    @Test fun `opt out waits a full offline day reconnect resets grace and current seats survive`() = runTest {
        var now=100L
        val own=profile("ABCDEF11","Michael")
        val recorder=StandaloneTable(MemoryStandaloneStore(StandaloneGame(),LocalLibrary()),wallClock={now},shortGameMs=0)
        recorder.addPlayer("Past player");recorder.addPlayer("Past opponent");recorder.start();now+=10_000
        recorder.control(com.turnhub.android.data.TabletSeat(9,1),"win")
        val history=recorder.state.value.library.history
        assertFalse(history.isEmpty())
        val store=MemoryStandaloneStore(StandaloneGame(),LocalLibrary(players=listOf(own),history=history))
        val table=StandaloneTable(store,wallClock={now})
        table.receiveSharedProfiles(atlas,listOf(SharedProfileCopy.encode(profile())))
        table.keepCopiedProfiles(false);table.observeCopyConnection(null)
        now+=day-1;table.maintainCopiedProfiles();assertEquals(2,table.state.value.library.players.size)
        table.observeCopyConnection(atlas);table.observeCopyConnection(null)
        now+=2;table.maintainCopiedProfiles();assertEquals(2,table.state.value.library.players.size)
        val copy=table.state.value.library.players.single { it.copiedFromAtlas!=null }
        table.selectPlayer(copy.localId);now+=day;table.maintainCopiedProfiles()
        assertEquals(2,table.state.value.library.players.size)
        table.clearPlayers()
        val restarted=StandaloneTable(store,wallClock={now});restarted.observeCopyConnection(null)
        assertEquals(listOf(own),restarted.state.value.library.players)
        assertEquals(history,restarted.state.value.library.history)
    }

    @Test fun `newer copied PIN relocks while stale data cannot remove it`() {
        val table=StandaloneTable(MemoryStandaloneStore(StandaloneGame(),LocalLibrary()))
        val first=profile()
        table.receiveSharedProfiles(atlas,listOf(SharedProfileCopy.encode(first)))
        val id=table.state.value.library.players.single().localId
        assertTrue(table.unlockAccount(id,"1234"))
        table.receiveSharedProfiles(atlas,listOf(SharedProfileCopy.encode(first.copy(credential=LocalCredential.create("5678"),lastActivityMs=11))))
        assertFalse(table.accountUnlocked(id));assertFalse(table.unlockAccount(id,"1234"));assertTrue(table.unlockAccount(id,"5678"))
        table.receiveSharedProfiles(atlas,listOf(SharedProfileCopy.encode(first.copy(credential=null))))
        assertNotNull(table.state.value.library.players.single().credential)
        assertFalse(table.unlockAccount(id,"1234"))
    }
}
