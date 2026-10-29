package com.turnhub.android.ui.home

import org.junit.Assert.assertEquals
import org.junit.Test

class GameSourceTest {
    @Test
    fun withoutAtlasTheDeviceRunsTheGame() {
        assertEquals(GameSource.DEVICE, gameSource(false, false, false, null))
        assertEquals(GameSource.DEVICE, gameSource(false, false, false, GameSource.ATLAS))
    }

    @Test
    fun atlasRunsTheGameUnlessTheDeviceHasOneUnderway() {
        assertEquals(GameSource.ATLAS, gameSource(true, false, false, null))
        assertEquals(GameSource.DEVICE, gameSource(true, false, true, null))
        assertEquals(GameSource.ATLAS, gameSource(true, true, true, null))
    }

    @Test
    fun thePlayersPickWinsWhileAtlasIsThere() {
        assertEquals(GameSource.DEVICE, gameSource(true, true, false, GameSource.DEVICE))
        assertEquals(GameSource.ATLAS, gameSource(true, false, true, GameSource.ATLAS))
    }
}
