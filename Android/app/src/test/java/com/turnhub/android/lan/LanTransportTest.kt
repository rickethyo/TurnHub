package com.turnhub.android.lan

import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test
import java.io.DataOutputStream
import java.io.File
import java.net.InetAddress
import java.net.ServerSocket
import java.net.Socket
import java.nio.file.Files
import java.security.KeyStore
import java.security.MessageDigest
import java.security.PrivateKey
import java.security.cert.X509Certificate
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicInteger
import javax.net.ssl.SSLContext

class LanTransportTest {
    @Test fun `invitation round trips and rejects public endpoints and malformed pins`() {
        val invite = LanInvite("192.168.1.5", 8443, "a".repeat(64), "b".repeat(32), "c".repeat(64))
        assertEquals(invite, LanInvite.parse(invite.encode()))
        assertFalse(invite.toString().contains(invite.key))
        listOf("8.8.8.8", "localhost", "127.0.0.1", "192.168.001.5", "192.168.1.256").forEach {
            assertTrue(runCatching { LanInvite.parse(invite.copy(address = it).encode()) }.isFailure)
        }
        assertTrue(runCatching { LanInvite.parse(invite.copy(pin = "bad").encode()) }.isFailure)
        assertTrue(runCatching { LanInvite.parse(invite.encode() + "?extra=1") }.isFailure)
        assertTrue(runCatching { LanInvite.parse(invite.copy(port = 80).encode()) }.isFailure)
    }

    @Test fun `bounded framing round trips and rejects oversized input before dispatch`() {
        val handled = AtomicInteger()
        val socket = ServerSocket(0, 8, InetAddress.getLoopbackAddress())
        LanServer(socket) { request -> handled.incrementAndGet(); request.put("reply", true) }.use { server ->
            Socket(InetAddress.getLoopbackAddress(), server.port).use { client ->
                client.soTimeout = 2_000
                LanFrames.write(client, JSONObject().put("unicode", "Elder One · é"))
                assertTrue(LanFrames.read(client).getBoolean("reply"))
            }
            Socket(InetAddress.getLoopbackAddress(), server.port).use { client ->
                client.soTimeout = 2_000
                DataOutputStream(client.getOutputStream()).apply { writeInt(LAN_MAX_FRAME + 1); flush() }
                assertEquals(-1, client.getInputStream().read())
            }
            listOf(("{\"nested\":".repeat(1000) + "0" + "}".repeat(1000)).toByteArray(),
                byteArrayOf(0xc0.toByte(), 0x80.toByte()), "{} {}".toByteArray()).forEach { bytes ->
                Socket(InetAddress.getLoopbackAddress(), server.port).use { client ->
                    client.soTimeout = 2_000
                    DataOutputStream(client.getOutputStream()).apply { writeInt(bytes.size); write(bytes); flush() }
                    assertEquals(-1, client.getInputStream().read())
                }
            }
            assertEquals(1, handled.get())
        }
    }

    @Test fun `pinned TLS accepts RSA hosts and rejects a different certificate pin`() = pinnedTls("RSA")

    @Test fun `pinned TLS accepts EC hosts and rejects a different certificate pin`() = pinnedTls("EC")

    private fun pinnedTls(algorithm: String) {
        val directory = Files.createTempDirectory("turnhub-lan-test").toFile()
        try {
            val path = File(directory, "test.p12")
            val keytool = File(System.getProperty("java.home"), "bin/keytool").absolutePath
            val process = ProcessBuilder(keytool, "-genkeypair", "-alias", "test", "-keystore", path.absolutePath,
                "-storetype", "PKCS12", "-storepass", "test-only-password", "-keypass", "test-only-password",
                "-dname", "CN=TurnHub test", "-keyalg", algorithm, "-keysize", if (algorithm == "EC") "256" else "2048", "-validity", "1", "-noprompt")
                .redirectErrorStream(true).redirectOutput(File(directory, "keytool.log")).start()
            assertTrue(process.waitFor(20, TimeUnit.SECONDS))
            assertEquals(0, process.exitValue())
            val store = KeyStore.getInstance("PKCS12").apply { path.inputStream().use { load(it, "test-only-password".toCharArray()) } }
            val manager = lanHostKeyManager(store.getKey("test", "test-only-password".toCharArray()) as PrivateKey,
                store.getCertificateChain("test").map { it as X509Certificate }.toTypedArray())
            assertNull(manager.getPrivateKey("another-alias"))
            assertNull(manager.getCertificateChain("another-alias"))
            assertNull(manager.chooseServerAlias(if (algorithm == "EC") "RSA" else "EC", null, null))
            val context = SSLContext.getInstance("TLS").apply { init(arrayOf(manager), null, null) }
            val pin = MessageDigest.getInstance("SHA-256").digest(store.getCertificate("test").encoded).hex()
            val socket = context.serverSocketFactory.createServerSocket(0, 8, InetAddress.getLoopbackAddress())
            val handled = AtomicInteger()
            LanServer(socket) { handled.incrementAndGet(); JSONObject().put("v", 1).put("session", "session").put("code", "ok") }.use { server ->
                val invite = LanInvite(InetAddress.getLoopbackAddress().hostAddress!!, server.port, pin, "session", "key")
                assertEquals("ok", lanExchange(invite, JSONObject().put("op", "test")).getString("code"))
                assertTrue(runCatching { lanExchange(invite.copy(pin = "0".repeat(64)), JSONObject()) }.isFailure)
                assertEquals(1, handled.get())
            }
        } finally { directory.deleteRecursively() }
    }
}
