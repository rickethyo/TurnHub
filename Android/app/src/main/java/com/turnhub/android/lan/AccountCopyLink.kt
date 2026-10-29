package com.turnhub.android.lan

import android.content.Context
import org.json.JSONObject
import java.net.InetSocketAddress
import java.util.concurrent.atomic.AtomicBoolean
import java.util.concurrent.atomic.AtomicInteger

/**
 * "Use on another device" over the same Wi-Fi (owner, 2026-10-29). The device holding the account
 * shows a QR code with an invitation like Play together's (address, certificate pin, session and
 * a long one-time key); the other device scans it and fetches the account over pinned TLS, so the
 * account itself is never in the picture. One fetch, then the key is spent.
 */
internal class AccountCopyHost private constructor(private val server: LanServer, val invitation: String) : AutoCloseable {
    override fun close() = server.close()

    companion object {
        /** Opens on the current Wi-Fi. Blocking: call it off the main thread. [onSent] runs on a worker thread. */
        fun open(context: Context, account: JSONObject, onSent: () -> Unit): AccountCopyHost {
            val wifi = currentLanWifi(context)
            val (tls, pin) = lanServerContext()
            val session = randomLanSecret(16)
            val key = randomLanSecret()
            val sent = AtomicBoolean(false)
            val failures = AtomicInteger(0)
            fun reply(code: String) = JSONObject().put("v", LAN_VERSION).put("session", session).put("code", code)
            val socket = tls.serverSocketFactory.createServerSocket()
            try {
                socket.bind(InetSocketAddress(wifi.address, 0), 4)
                val server = LanServer(socket) { request ->
                    try {
                        require(request.integer("v") == LAN_VERSION.toLong() && request.text("session") == session &&
                            request.text("op") == "account")
                        when {
                            failures.get() >= 10 -> reply("locked")
                            !sameSecret(request.text("key"), key) -> { failures.incrementAndGet(); reply("unauthorized") }
                            !sent.compareAndSet(false, true) -> reply("used")
                            else -> reply("ok").put("account", account).also { onSent() }
                        }
                    } catch (_: Exception) { reply("invalid") }
                }
                return AccountCopyHost(server, LanInvite(wifi.address.hostAddress!!, server.port, pin, session, key).encode("account"))
            } catch (error: Throwable) { socket.close(); throw error }
        }

        /** Whether [text] is an account invitation (a scanned QR code). */
        fun isInvitation(text: String) = runCatching { LanInvite.parse(text, "account") }.isSuccess

        /** Fetches the account an invitation offers. Blocking: call it off the main thread. */
        fun fetch(context: Context, text: String): JSONObject {
            val invite = LanInvite.parse(text, "account")
            val wifi = currentLanWifi(context)
            val reply = lanExchange(invite, JSONObject().put("v", LAN_VERSION).put("session", invite.session)
                .put("op", "account").put("key", invite.key)) { wifi.network.bindSocket(it) }
            return when (reply.text("code")) {
                "ok" -> reply.getJSONObject("account")
                "used" -> throw AccountCopyRefused("That code was already used. Show a new one on the other device.")
                else -> throw AccountCopyRefused("The other device didn't share the account. Show a new code and try again.")
            }
        }
    }
}

/** The other device answered but didn't hand over the account; [message] says why, for the person. */
internal class AccountCopyRefused(message: String) : Exception(message)
