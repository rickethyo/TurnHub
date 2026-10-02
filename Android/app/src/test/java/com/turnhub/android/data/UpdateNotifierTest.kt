package com.turnhub.android.data

import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class UpdateNotifierTest {
    @Test
    fun `notifies when more devices are behind than last time, never for none`() {
        assertTrue(UpdateNotifier.shouldNotify(0, 1))
        assertTrue(UpdateNotifier.shouldNotify(1, 2))
        assertFalse(UpdateNotifier.shouldNotify(2, 2))
        assertFalse(UpdateNotifier.shouldNotify(2, 1))
        assertFalse(UpdateNotifier.shouldNotify(0, 0))
    }
}
