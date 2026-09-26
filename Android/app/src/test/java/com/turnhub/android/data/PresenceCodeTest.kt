package com.turnhub.android.data

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class PresenceCodeTest {

    @Test
    fun `reads the code from the Atlas admin-code QR`() {
        assertEquals("042917", presenceCodeFromQr("http://192.168.4.1/portal#code=042917"))
        assertEquals("042917", presenceCodeFromQr(" 042917 "))
    }

    @Test
    fun `ignores the other Atlas QR codes and malformed codes`() {
        assertNull(presenceCodeFromQr("WIFI:T:WPA;S:TurnHub-Atlas;P:TurnHub-Setup;;"))
        assertNull(presenceCodeFromQr("http://192.168.4.1/portal"))
        assertNull(presenceCodeFromQr("http://192.168.4.1/portal#code=12345"))
        assertNull(presenceCodeFromQr("http://192.168.4.1/portal#code=1234567"))
    }
}
