package com.turnhub.android.data

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertThrows
import org.junit.Assert.assertTrue
import org.junit.Test
import java.io.IOException
import java.security.MessageDigest

class FirmwareReleasesTest {

    private fun sha(bytes: ByteArray) = MessageDigest.getInstance("SHA-256").digest(bytes).joinToString("") { "%02x".format(it) }

    private val atlasBytes = ByteArray(300) { it.toByte() }
    private val feedJson = """
        {"schema":1,"release":"0.9.0","packages":[
          {"product":"atlas","version":"0.7.0","radioProtocol":2,"file":"atlas-0.7.0.thfw","size":300,
           "sha256":"${sha(atlasBytes)}","buildId":"abc"},
          {"product":"sigil-oled","version":"0.9.0","file":"sigil-oled-0.9.0.thfw","size":10,"sha256":"${"a".repeat(64)}"},
          {"product":"sigil-eink","version":"0.9.0","file":"sigil-eink-0.9.0.thfw","size":10,"sha256":"not-a-hash"},
          {"product":"toaster","version":"1.0.0","file":"toaster.thfw","size":10,"sha256":"${"b".repeat(64)}"}
        ]}
    """.trimIndent()

    private fun device(id: Int, display: String, firmware: String, capabilities: Int = 0, online: Boolean = true) =
        DeviceInfo(id, "Sigil ${id + 1}", "Sigil ${id + 1}", "", "THS-$id", online, firmware, display, 0, capabilities)

    @Test
    fun `versions compare numerically and ignore suffixes`() {
        assertEquals(FirmwareVersion(0, 6, 0), FirmwareVersion.parse("0.6.0-dev"))
        assertTrue(FirmwareVersion.parse("0.10.0")!! > FirmwareVersion.parse("0.9.9")!!)
        assertNull(FirmwareVersion.parse("unknown"))
    }

    @Test
    fun `the feed keeps well-formed known packages only`() {
        val feed = FirmwareReleaseFeed.parse(feedJson)
        assertEquals("0.9.0", feed.release)
        assertEquals(listOf(FirmwareProduct.ATLAS, FirmwareProduct.SIGIL_OLED), feed.packages.map { it.product })
        assertThrows(IllegalArgumentException::class.java) { FirmwareReleaseFeed.parse("""{"schema":2,"packages":[]}""") }
        assertThrows(IllegalArgumentException::class.java) { FirmwareReleaseFeed.parse("<html>") }
    }

    @Test
    fun `the plan covers Atlas and every real Sigil`() {
        val feed = FirmwareReleaseFeed.parse(feedJson)
        val plan = UpdatePlan.build(
            feed,
            "0.6.0-dev",
            listOf(
                device(1, "oled", "0.8.2"),
                device(0, "oled", "0.9.0"),
                device(2, "epaper", "0.8.0"), // No valid e-ink package in this feed.
                device(3, "unknown", "unknown"),
                device(4, "oled", "0.8.0", capabilities = DeviceInfo.CAPABILITY_HARNESS),
            ),
        )
        assertEquals(listOf("Atlas", "Sigil 1", "Sigil 2", "Sigil 3", "Sigil 4"), plan.targets.map { it.label })
        assertEquals(listOf("Atlas", "Sigil 2"), plan.pending.map { it.label })
        assertTrue(plan.anyUpdate)
        assertFalse(UpdatePlan.build(feed, "0.7.0", emptyList()).anyUpdate)
    }

    @Test
    fun `downloads must match the feed exactly`() {
        val pkg = FirmwareReleaseFeed.parse(feedJson).packageFor(FirmwareProduct.ATLAS)!!
        verifyPackage(pkg, atlasBytes)
        assertThrows(IOException::class.java) { verifyPackage(pkg, atlasBytes.copyOf(299)) }
        val tampered = atlasBytes.copyOf().also { it[0] = 99 }
        assertThrows(IOException::class.java) { verifyPackage(pkg, tampered) }
    }

    @Test
    fun `the portal pack installs first, only where there is a card`() {
        val json = """
            {"schema":1,"release":"0.9.1","packages":[
              {"product":"atlas","version":"0.7.0","file":"atlas-0.7.0.thfw","size":300,"sha256":"${sha(atlasBytes)}"},
              {"product":"portal","version":"1.2.0","file":"portal-1.2.0.thfw","size":6000000,"sha256":"${"c".repeat(64)}"}
            ]}
        """.trimIndent()
        // Larger than a firmware slot but within the pack limit.
        val feed = FirmwareReleaseFeed.parse(json)
        assertEquals(FirmwareVersion(1, 2, 0), feed.packageFor(FirmwareProduct.PORTAL)?.version)

        val fresh = UpdatePlan.build(feed, "0.7.0", emptyList(), PortalStatus.parse("""{"card":true,"installed":false,"version":""}"""))
        assertEquals(listOf("Web portal", "Atlas"), fresh.targets.map { it.label })
        assertEquals(listOf("Web portal"), fresh.pending.map { it.label })

        val current = PortalStatus.parse("""{"card":true,"installed":true,"version":"1.2.0"}""")
        assertFalse(UpdatePlan.build(feed, "0.7.0", emptyList(), current).anyUpdate)

        val noCard = PortalStatus.parse("""{"card":false,"installed":false,"version":""}""")
        assertEquals(listOf("Atlas"), UpdatePlan.build(feed, "0.7.0", emptyList(), noCard).targets.map { it.label })
        assertNull(PortalStatus.parse("<html>"))
    }
}
