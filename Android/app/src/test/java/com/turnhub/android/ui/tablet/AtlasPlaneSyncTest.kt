package com.turnhub.android.ui.tablet

import org.junit.Assert.assertEquals
import org.junit.Test

class AtlasPlaneSyncTest {
    // Counts the syncs that reach Atlas; the block itself isn't run (no Atlas here).
    private var syncs = 0
    // The block never runs here, so the sync is marked finished by hand.
    private val actions = TabletActions(run = { syncs++; AtlasPlaneSync.running = false })

    @Test fun `the lobby syncs on its own once per Atlas boot, never again after a failure`() {
        AtlasPlaneSync.send(actions, emptyList(), all = false, automaticFor = "atlas-1/boot-a") { null }
        // The same boot again (a reconnect after a slow or failed sync): nothing.
        AtlasPlaneSync.send(actions, emptyList(), all = false, automaticFor = "atlas-1/boot-a") { null }
        assertEquals(1, syncs)
        // Atlas restarted, or another Atlas: once more.
        AtlasPlaneSync.send(actions, emptyList(), all = false, automaticFor = "atlas-1/boot-b") { null }
        assertEquals(2, syncs)
        // Send planes to Atlas always asks.
        AtlasPlaneSync.send(actions, emptyList(), all = true) { null }
        AtlasPlaneSync.send(actions, emptyList(), all = true) { null }
        assertEquals(4, syncs)
    }
}
