package com.turnhub.android.data

import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class ProfileSecretTest {
    @Test
    fun `PINs and passwords follow Atlas's rule`() {
        for (ok in listOf("1234", "12345678", "correct horse", "Ünïcode pass 9", "a".repeat(64))) {
            assertTrue(ok, ProfileSecret.isValid(ok))
        }
        for (bad in listOf("", "123", "pass1", "seven77", "tab\there!", "a".repeat(65), "ü".repeat(33))) {
            assertFalse(bad, ProfileSecret.isValid(bad))
        }
    }
}
