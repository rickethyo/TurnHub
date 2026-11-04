package com.turnhub.android.domain

import com.turnhub.android.protocol.AtlasHub
import com.turnhub.android.protocol.TableState
import com.turnhub.android.testing.Fixtures
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertThrows
import org.junit.Assert.assertTrue
import org.junit.Test

class TableSummaryMapperTest {

    @Test
    fun `an Atlas summary carries identity, battery, links and the hub, never players`() {
        val summary = TableSummaryMapper.map(Fixtures.info(), Fixtures.state("state-hosted.response.json"), receivedAtMs = 5)

        assertEquals("THA-025448410001", summary.atlasId)
        assertEquals(Fixtures.BOOT_ID, summary.bootId)
        assertEquals("0.6.0-dev", summary.firmwareVersion)
        assertEquals(7L, summary.revision)
        assertEquals(5L, summary.receivedAtMs)
        assertEquals(AtlasHub("Kitchen tablet", "m-20261104-1", TableState.RUNNING, 4), summary.hub)
        assertEquals(64, summary.battery?.percent)
        assertEquals(2, summary.links?.sigils)
        assertTrue(summary.players.isEmpty() && summary.physicalSigils.isEmpty())
        assertEquals(TableState.LOBBY, summary.state)
    }

    @Test
    fun `an idle Atlas has no hub`() {
        val summary = TableSummaryMapper.map(Fixtures.info(), Fixtures.state("state-idle.response.json"))
        assertNull(summary.hub)
        assertNull(summary.battery)
    }

    @Test
    fun `controller handles split physical and virtual`() {
        assertEquals(ControllerHandle.Kind.PHYSICAL, ControllerHandle(0).kind)
        assertEquals(ControllerHandle.Kind.PHYSICAL, ControllerHandle(7).kind)
        assertEquals(ControllerHandle.Kind.VIRTUAL, ControllerHandle(8).kind)
        assertEquals(ControllerHandle.Kind.VIRTUAL, ControllerHandle(23).kind)
        assertEquals(ControllerHandle.Kind.UNKNOWN, ControllerHandle(24).kind)
    }

    @Test
    fun `refuses to combine info and state from different boots`() {
        val otherBoot = Fixtures.state("state-idle.response.json") { put("bootId", Fixtures.OTHER_BOOT_ID) }
        assertThrows(IllegalArgumentException::class.java) { TableSummaryMapper.map(Fixtures.info(), otherBoot) }
    }
}
