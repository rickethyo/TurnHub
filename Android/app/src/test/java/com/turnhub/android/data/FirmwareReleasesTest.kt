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
          {"product":"sigil","version":"0.9.0","file":"sigil-0.9.0.thfw","size":10,"sha256":"${"a".repeat(64)}"},
          {"product":"sigil","version":"0.9.1","file":"sigil-0.9.1.thfw","size":10,"sha256":"not-a-hash"},
          {"product":"sigil-eink","version":"0.9.0","file":"sigil-eink-0.9.0.thfw","size":10,"sha256":"${"a".repeat(64)}"},
          {"product":"toaster","version":"1.0.0","file":"toaster.thfw","size":10,"sha256":"${"b".repeat(64)}"}
        ]}
    """.trimIndent()

    private fun device(id: Int, firmware: String, capabilities: Int = 0, online: Boolean = true) =
        DeviceInfo(id, "Sigil ${id + 1}", "Sigil ${id + 1}", "", "THS-$id", online, firmware, 0, capabilities)

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
        assertEquals(listOf(FirmwareProduct.ATLAS, FirmwareProduct.SIGIL), feed.packages.map { it.product })
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
                device(1, "0.8.2"),
                device(0, "0.9.0"),
                device(2, "0.9.1"),
                device(3, "unknown"), // Unknown firmware: offer the release.
                device(4, "0.8.0", capabilities = DeviceInfo.CAPABILITY_HARNESS),
            ),
        )
        assertEquals(listOf("Atlas", "Sigil 1", "Sigil 2", "Sigil 3", "Sigil 4"), plan.targets.map { it.label })
        assertEquals(listOf("Atlas", "Sigil 2", "Sigil 4"), plan.pending.map { it.label })
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
    fun `retired portal packages are ignored and Atlas carries the portal`() {
        val feed = FirmwareReleaseFeed.parse("""
            {"schema":1,"packages":[
              {"product":"atlas","version":"0.7.7","file":"atlas-0.7.7.thfw","size":300,"sha256":"${sha(atlasBytes)}"},
              {"product":"portal","version":"2.0.0","file":"portal-2.0.0.thfw","size":6000000,"sha256":"${"c".repeat(64)}"}
            ]}
        """.trimIndent())
        assertEquals(listOf(FirmwareProduct.ATLAS), feed.packages.map { it.product })
        val plan = UpdatePlan.build(feed, "0.7.6", emptyList())
        assertEquals(listOf("Atlas"), plan.pending.map { it.label })
    }
}
