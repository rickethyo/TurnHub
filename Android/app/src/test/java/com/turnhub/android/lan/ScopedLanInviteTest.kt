package com.turnhub.android.lan

import org.junit.Assert.*
import org.junit.Test

class ScopedLanInviteTest {
    @Test fun `scoped invitation retains room address and only its player key`() {
        val room = LanInvite("192.168.1.5", 3456, "a".repeat(64), "b".repeat(32), "abc-123")
        val scoped = room.copy(key = "c".repeat(64), profileOnly = "ABCDEF01")
        assertEquals(scoped, LanInvite.parse(scoped.encode()))
        assertEquals(room, LanInvite.parse(room.encode()))
        assertFalse(scoped.encode().contains("abc-123"))
        assertTrue(runCatching { LanInvite.parse(scoped.encode() + "/another") }.isFailure)
        assertTrue(runCatching { LanInvite.parse(scoped.encode().replace("ABCDEF01", "invalid")) }.isFailure)
    }
}
