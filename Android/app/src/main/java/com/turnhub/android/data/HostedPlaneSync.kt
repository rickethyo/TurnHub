package com.turnhub.android.data

import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.currentCoroutineContext
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.collectLatest
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.isActive
import kotlinx.coroutines.withContext
import org.json.JSONObject
import java.util.Base64

/** Sends the hosted game's planes to Atlas; only its saved card index confirms delivery. */
class HostedPlaneSync(
    private val call: suspend (method: String, path: String, fields: List<Pair<String, String>>) -> RawResponse?,
    private val callText: suspend (path: String, fields: List<Pair<String, String>>, body: String) -> RawResponse?,
    private val catalog: () -> List<PlaneInfo>?,
    private val art: suspend (PlaneInfo) -> ByteArray?,
    private val busyWaitMs: Long = 1_500,
    private val retryMs: Long = 5_000,
    private val encodeArt: suspend (ByteArray) -> String? = { bytes ->
        withContext(Dispatchers.Default) { AtlasPlaneCards.artJpeg(bytes)?.let { Base64.getEncoder().encodeToString(it) } }
    },
    private val report: (String?) -> Unit = {},
    private val ioDispatcher: CoroutineDispatcher = Dispatchers.IO,
) {
    /** Retries a quiet game's missing cards, late catalog download, storage and network recovery. */
    suspend fun run(wanted: Flow<List<String>>) {
        try {
            wanted.distinctUntilChanged().collectLatest { ids ->
                if (ids.isEmpty()) { report(null); return@collectLatest }
                while (currentCoroutineContext().isActive) {
                    try { sync(ids) }
                    catch (e: CancellationException) { throw e }
                    catch (_: Exception) { report("Plane transfer interrupted. Retrying automatically.") }
                    // A 202 only queued a write. Re-read Atlas even after a successful send, and after a restart.
                    delay(retryMs)
                }
            }
        } finally { report(null) }
    }

    private suspend fun sync(ids: List<String>) {
        val cards = withContext(ioDispatcher) { catalog() }?.associateBy { it.id }
        if (cards == null || ids.none { it in cards }) {
            report("Waiting for this phone to download the planes.")
            return
        }
        val lines = (ids.mapNotNull { cards[it] } + cards.values.filter { it.id !in ids }).take(MAX_PLANES)
        val version = "${lines.size}-" + Integer.toHexString(lines.map { it.id }.sorted().hashCode())
        val info = request { call("GET", "/api/planes", emptyList()) }
        if (info?.ok != true) { failed(info); return }
        val known = JSONObject(info.body)
        if (known.optString("version") != version) {
            val body = lines.joinToString("\n") { card ->
                "${card.id}\t${AtlasPlaneCards.ascii(card.name).take(40)}\t${if (card.phenomenon) 1 else 0}"
            }
            val sent = request { callText("/api/planes", listOf("version" to version), body) }
            if (sent?.ok != true) { failed(sent); return }
        }
        fun saved(key: String): Set<String> = known.optJSONArray(key)?.let { list ->
            (0 until list.length()).map { list.getString(it) }.toSet()
        }.orEmpty()
        val savedCards = saved("cards")
        val savedArt = saved("art")
        val missing = ids.filter { it in cards && it !in savedArt }.take(MAX_PLANES)
        if (missing.isEmpty()) {
            report(if (ids.any { it !in cards }) "Waiting for this phone to download the remaining planes."
                else "Planes and artwork saved on Atlas.")
            return
        }
        report("Sending planes to Atlas...")
        missing.forEachIndexed { i, id ->
            val card = cards.getValue(id)
            val text = AtlasPlaneCards.text(card)
            // Text can arrive first. Missing artwork is retried; it is never marked as saved.
            val encoded = art(card)?.let { encodeArt(it) }
            if (encoded == null && id in savedCards) return@forEachIndexed
            val sent = request {
                callText("/api/planes/card", listOf("id" to id, "type" to text.type, "artist" to text.artist,
                    "text" to text.text, "chaos" to text.chaos, "index" to "${i + 1}", "total" to "${missing.size}"), encoded.orEmpty())
            }
            if (sent?.ok != true) { failed(sent); return }
        }
        report("Waiting for plane artwork to save. Retrying automatically.")
    }

    private fun failed(response: RawResponse?) {
        report(when (response?.code) {
            401 -> "Sign in to Atlas to send planes."
            409 -> "Atlas couldn't save the planes. Check its microSD card; retrying automatically."
            else -> "Atlas couldn't receive the planes. Retrying automatically."
        })
    }

    private suspend fun request(send: suspend () -> RawResponse?): RawResponse? {
        repeat(BUSY_TRIES) {
            val response = send() ?: return null
            if (response.code != 503) return response
            report("Atlas is saving plane cards. Waiting...")
            delay(busyWaitMs)
        }
        return send()
    }

    companion object {
        const val MAX_PLANES = 192
        private const val BUSY_TRIES = 20
        const val AHEAD = 3
        fun wantedPlanes(planes: com.turnhub.android.standalone.Planechase): List<String> =
            if (!planes.enabled || planes.physical) emptyList() else (planes.faceUp + planes.deck.take(AHEAD)).map { it.id }.distinct()
    }
}
