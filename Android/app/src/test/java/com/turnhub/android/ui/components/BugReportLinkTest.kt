package com.turnhub.android.ui.components

import java.net.URI
import java.net.URLDecoder
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class BugReportLinkTest {
    private val phone = BugReportDevice(
        version = "0.1.0 · Build 123 · Debug", manufacturer = "Example", model = "Phone",
        androidVersion = "16", apiLevel = 36, smallestWidthDp = 411, widthDp = 411, heightDp = 891,
    )

    private fun answers(url: String): Map<String, String> = URI(url).rawQuery.split("&").associate {
        val (key, value) = it.split("=", limit = 2)
        key to URLDecoder.decode(value, "UTF-8")
    }

    @Test fun `local report fills the actual build and device without requiring an Atlas`() {
        val url = bugReportUrl(phone, BugReportContext(BugReportMode.ON_DEVICE, "Table"))
        assertEquals("https", URI(url).scheme)
        assertEquals("docs.google.com", URI(url).host)
        assertTrue(URI(url).path.endsWith("/viewform"))
        assertFalse(url.contains("formResponse"))
        val fields = answers(url)
        assertEquals(phone.version, fields["entry.${BugReportEntries.APP_VERSION}"])
        assertEquals("Phone", fields["entry.${BugReportEntries.DEVICE_TYPE}"])
        assertEquals("Playing on one device", fields["entry.${BugReportEntries.MODE}"])
        assertTrue(fields.getValue("entry.${BugReportEntries.DEVICE_DETAILS}").contains("Android: 16 (API 36)"))
        assertEquals(setOf("usp", "entry.${BugReportEntries.APP_VERSION}", "entry.${BugReportEntries.DEVICE_TYPE}",
            "entry.${BugReportEntries.MODE}", "entry.${BugReportEntries.DEVICE_DETAILS}"), fields.keys)
    }

    @Test fun `connected report includes firmware and relevant game context`() {
        val fields = answers(bugReportUrl(phone, BugReportContext(
            BugReportMode.ATLAS, "Full-screen table", "0.7.15", "mtg_commander", "PAUSED")))
        assertEquals("Connected to Atlas", fields["entry.${BugReportEntries.MODE}"])
        val details = fields.getValue("entry.${BugReportEntries.DEVICE_DETAILS}")
        assertTrue(details.contains("Atlas firmware (last known): 0.7.15"))
        assertTrue(details.contains("Game: mtg_commander"))
        assertTrue(details.contains("State: PAUSED"))
    }

    @Test fun `tablet classification includes the 600 dp boundary`() {
        assertEquals("Phone", phone.copy(smallestWidthDp = 599).type)
        val tablet = phone.copy(smallestWidthDp = 600, widthDp = 960, heightDp = 600)
        val fields = answers(bugReportUrl(tablet, BugReportContext()))
        assertEquals("Tablet", fields["entry.${BugReportEntries.DEVICE_TYPE}"])
        assertTrue(fields.getValue("entry.${BugReportEntries.DEVICE_DETAILS}").contains("960 x 600 dp"))
    }

    @Test fun `unknown context does not guess play mode device type or app version`() {
        val unknown = phone.copy(version = null, smallestWidthDp = 0)
        val fields = answers(bugReportUrl(unknown, BugReportContext()))
        assertFalse(fields.containsKey("entry.${BugReportEntries.APP_VERSION}"))
        assertFalse(fields.containsKey("entry.${BugReportEntries.MODE}"))
        assertFalse(fields.containsKey("entry.${BugReportEntries.DEVICE_TYPE}"))
    }

    @Test fun `local play does not inherit background Atlas firmware`() {
        val fields = answers(bugReportUrl(phone, BugReportContext(
            BugReportMode.ON_DEVICE, "Game", atlasFirmware = "0.7.15")))
        assertEquals("Playing on one device", fields["entry.${BugReportEntries.MODE}"])
        assertFalse(fields.getValue("entry.${BugReportEntries.DEVICE_DETAILS}").contains("Atlas firmware"))
    }

    @Test fun `special characters and unicode cannot inject form fields`() {
        val device = phone.copy(version = "Build &entry.1=injected +?", model = "Café 日本語 & test")
        val report = BugReportContext(screen = "A&B? # + line\nnext")
        val url = bugReportUrl(device, report)
        val fields = answers(url)
        assertEquals(device.version, fields["entry.${BugReportEntries.APP_VERSION}"])
        val details = fields.getValue("entry.${BugReportEntries.DEVICE_DETAILS}")
        assertTrue(details.contains(device.model))
        assertTrue(details.contains(report.screen!!))
        assertFalse(fields.containsKey("entry.1"))
        assertEquals(null, URI(url).fragment)
    }
}
