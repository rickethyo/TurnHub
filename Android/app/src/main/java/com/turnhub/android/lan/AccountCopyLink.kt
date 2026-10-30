package com.turnhub.android.lan

import android.content.Context
import org.json.JSONObject
import java.net.InetSocketAddress
import java.util.concurrent.atomic.AtomicBoolean
import java.util.concurrent.atomic.AtomicInteger
import java.util.concurrent.atomic.AtomicReference
import org.json.JSONArray

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
            val claimedBy = AtomicReference<String?>(null)
            val history = account.optJSONArray("history") ?: JSONArray()
            val pages = (history.length() + 31) / 32
            val metadata = JSONObject(account.toString()).apply { remove("history"); put("historyPages", pages); put("historyCount", history.length()) }
            val failures = AtomicInteger(0)
            fun reply(code: String) = JSONObject().put("v", LAN_VERSION).put("session", session).put("code", code)
            val socket = tls.serverSocketFactory.createServerSocket()
            try {
                socket.bind(InetSocketAddress(wifi.address, 0), 4)
                val server = LanServer(socket) { request ->
                    try {
                        require(request.integer("v") == LAN_VERSION.toLong() && request.text("session") == session)
                        val op = request.text("op")
                        val transfer = request.text("transfer")
                        require(transfer.matches(Regex("[0-9a-fA-F]{64}")))
                        when {
                            failures.get() >= 10 -> reply("locked")
                            !sameSecret(request.text("key"), key) -> { failures.incrementAndGet(); reply("unauthorized") }
                            sent.get() -> reply("used")
                            !claimedBy.compareAndSet(null, transfer) && claimedBy.get() != transfer -> reply("used")
                            op == "account" -> reply("ok").put("account", metadata)
                            op == "account-page" -> {
                                val page = request.integer("page").toInt()
                                require(page in 0 until pages)
                                reply("ok").put("page", page).put("history", JSONArray().apply {
                                    for (i in page * 32 until minOf(history.length(), (page + 1) * 32)) put(history.get(i))
                                })
                            }
                            op == "account-done" -> reply("ok").also { if (sent.compareAndSet(false, true)) onSent() }
                            else -> reply("invalid")
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
            val transfer = randomLanSecret()
            fun request(op: String, page: Int = -1): JSONObject = lanExchange(invite,
                JSONObject().put("v", LAN_VERSION).put("session", invite.session).put("op", op)
                    .put("key", invite.key).put("transfer", transfer).put("page", page)) { wifi.network.bindSocket(it) }
            val reply = request("account")
            return when (reply.text("code")) {
                "ok" -> reply.getJSONObject("account").also { account ->
                    // Older peers send the whole account in one frame. New copies page history.
                    if (account.has("historyPages")) {
                        val history = JSONArray()
                        val count = account.getInt("historyCount")
                        val pages = account.getInt("historyPages")
                        require(count >= 0 && pages == (count + 31) / 32)
                        repeat(pages) { page ->
                            val response = request("account-page", page)
                            require(response.text("code") == "ok" && response.getInt("page") == page)
                            val batch = response.getJSONArray("history")
                            require(batch.length() == minOf(32, count - page * 32))
                            for (i in 0 until batch.length()) history.put(batch.get(i))
                        }
                        account.put("history", history)
                        require(request("account-done").text("code") == "ok")
                    }
                }
                "used" -> throw AccountCopyRefused("That code was already used. Show a new one on the other device.")
                else -> throw AccountCopyRefused("The other device didn't share the account. Show a new code and try again.")
            }
        }
    }
}

/** The other device answered but didn't hand over the account; [message] says why, for the person. */
internal class AccountCopyRefused(message: String) : Exception(message)
