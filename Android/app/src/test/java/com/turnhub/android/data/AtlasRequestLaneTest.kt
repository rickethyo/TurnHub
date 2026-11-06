package com.turnhub.android.data

import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.launch
import kotlinx.coroutines.test.runCurrent
import kotlinx.coroutines.test.runTest
import kotlinx.coroutines.ExperimentalCoroutinesApi
import org.junit.Assert.*
import org.junit.Test

@OptIn(ExperimentalCoroutinesApi::class)
class AtlasRequestLaneTest {
    @Test fun `same Atlas requests wait and a cancelled waiter never opens a connection`() = runTest {
        val release = CompletableDeferred<Unit>()
        val events = mutableListOf<String>()
        val first = launch { AtlasRequestLane.run("atlas-one") { events += "first"; release.await(); events += "done" } }
        runCurrent()
        val cancelled = launch { AtlasRequestLane.run("atlas-one") { events += "cancelled" } }
        val next = launch { AtlasRequestLane.run("atlas-one") { events += "next" } }
        runCurrent(); assertEquals(listOf("first"), events)
        cancelled.cancel(); runCurrent(); release.complete(Unit)
        first.join(); next.join()
        assertEquals(listOf("first", "done", "next"), events)
    }

    @Test fun `different Atlases do not block each other and failures release the lane`() = runTest {
        val release = CompletableDeferred<Unit>()
        val events = mutableListOf<String>()
        val first = launch { try { AtlasRequestLane.run("atlas-one") { release.await(); error("network down") } } catch (_: IllegalStateException) { } }
        runCurrent()
        AtlasRequestLane.run("atlas-two") { events += "other" }
        val next = launch { AtlasRequestLane.run("atlas-one") { events += "next" } }
        runCurrent(); assertEquals(listOf("other"), events)
        release.complete(Unit); first.join(); next.join()
        assertEquals(listOf("other", "next"), events)
    }
}
