package com.turnhub.android.data

import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.collectLatest
import kotlinx.coroutines.flow.distinctUntilChanged
import org.json.JSONObject

/**
 * The hub's digital Planechase on the Atlas screen (owner, 2026-11-05): while this
 * phone hosts a game, Atlas gets the plane names (`POST /api/planes`) and each
 * plane's card text and art (`POST /api/planes/card`) it lacks, face-up plane
 * first, so its Planes tab and full-screen card can show them. Atlas keeps them
 * on its microSD card; the game itself stays on this phone.
 */
class HostedPlaneSync(
    private val call: suspend (method: String, path: String, fields: List<Pair<String, String>>) -> RawResponse?,
    private val callText: suspend (path: String, fields: List<Pair<String, String>>, body: String) -> RawResponse?,
    /** The planes downloaded on this phone, or null before the first download. */
    private val catalog: () -> List<PlaneInfo>?,
    /** A plane's art crop, or null when it can't be fetched. */
    private val art: suspend (PlaneInfo) -> ByteArray?,
    private val busyWaitMs: Long = 1_500,
) {
    /**
     * Runs until cancelled. [wanted] is the hub game's digital planes, face-up first
     * then the next few deck cards (empty when the game has no digital deck).
     */
    suspend fun run(wanted: Flow<List<String>>) {
        var catalogVersion: String? = null
        val onAtlas = mutableSetOf<String>()
        wanted.distinctUntilChanged().collectLatest { ids ->
            if (ids.isEmpty()) return@collectLatest
            val cards = withContext(Dispatchers.IO) { catalog() }?.associateBy { it.id } ?: return@collectLatest
            try {
                // The catalog: this game's planes first (Atlas keeps at most 192).
                val lines = (ids.mapNotNull { cards[it] } + cards.values.filter { it.id !in ids }).take(MAX_PLANES)
                val version = "${lines.size}-" + Integer.toHexString(lines.map { it.id }.sorted().hashCode())
                if (catalogVersion != version) {
                    val info = request { call("GET", "/api/planes", emptyList()) } ?: return@collectLatest
                    if (!info.ok) return@collectLatest
                    val known = JSONObject(info.body)
                    if (known.optString("version") != version) {
                        val body = lines.joinToString("\n") { card ->
                            "${card.id}\t${AtlasPlaneCards.ascii(card.name).take(40)}\t${if (card.phenomenon) 1 else 0}"
                        }
                        val sent = request { callText("/api/planes", listOf("version" to version), body) } ?: return@collectLatest
                        if (!sent.ok) return@collectLatest
                    }
                    known.optJSONArray("art")?.let { list -> for (i in 0 until list.length()) onAtlas += list.getString(i) }
                    catalogVersion = version
                }
                val missing = ids.filter { it !in onAtlas && it in cards }.take(MAX_PLANES)
                missing.forEachIndexed { i, id ->
                    val card = cards.getValue(id)
                    val text = AtlasPlaneCards.text(card)
                    // Decoding and re-encoding the art is too heavy for the main thread.
                    val encoded = art(card)?.let { crop ->
                        withContext(Dispatchers.Default) {
                            AtlasPlaneCards.artJpeg(crop)?.let { android.util.Base64.encodeToString(it, android.util.Base64.NO_WRAP) }
                        }
                    } ?: ""
                    val response = request {
                        callText("/api/planes/card", listOf("id" to id, "type" to text.type, "artist" to text.artist,
                            "text" to text.text, "chaos" to text.chaos, "index" to "${i + 1}", "total" to "${missing.size}"), encoded)
                    } ?: return@collectLatest
                    // A refusal (no microSD card, or a changed catalog) stops until the planes change.
                    if (!response.ok) return@collectLatest
                    onAtlas += id
                }
            } catch (e: CancellationException) { throw e } catch (e: Exception) {
                android.util.Log.w("HostedPlaneSync", "Plane sync to Atlas failed", e)
            }
        }
    }

    /** Sent again while Atlas answers 503 (its card index is being read, or its save queue is full). */
    private suspend fun request(send: suspend () -> RawResponse?): RawResponse? {
        repeat(BUSY_TRIES) {
            val response = send() ?: return null
            if (response.code != 503) return response
            delay(busyWaitMs)
        }
        return send()
    }

    companion object {
        /** Atlas's catalog limit (`TurnHubCards::MAX_PLANES`). */
        const val MAX_PLANES = 192
        private const val BUSY_TRIES = 20

        /** How many deck cards ahead Atlas gets, so the next planeswalk shows at once. */
        const val AHEAD = 3

        /** The game's digital planes for [run]: face-up first, then the next few; empty with the players' own cards. */
        fun wantedPlanes(planes: com.turnhub.android.standalone.Planechase): List<String> =
            if (!planes.enabled || planes.physical) emptyList() else (planes.faceUp + planes.deck.take(AHEAD)).map { it.id }.distinct()
    }
}
