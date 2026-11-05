package com.turnhub.android.data

import android.graphics.Bitmap
import android.graphics.BitmapFactory
import java.io.ByteArrayOutputStream

/** A plane's text for the Atlas card view: rules, the chaos ability apart, in Atlas's plain ASCII. */
data class AtlasPlaneText(val type: String, val artist: String, val text: String, val chaos: String)

/**
 * What the app sends Atlas for its Planechase card view (`POST /api/planes/card`): each
 * catalog plane's text and its art, cut to the 480x320 landscape screen. Card text and art
 * stay Scryfall's and Wizards of the Coast's; Atlas shows the artist and Scryfall credit.
 */
object AtlasPlaneCards {
    const val ART_WIDTH = 480
    const val ART_HEIGHT = 320
    /**
     * The largest JPEG the app sends. Atlas keeps up to 96 KB, but its web server
     * (arduino-esp32 WString) corrupts a form field that grows past 64 KB while it
     * URL-decodes it, so the base64 art must stay under that: 46 KB is about 62 KB encoded.
     */
    const val MAX_ART_BYTES = 46 * 1024

    /** Atlas's fonts are printable ASCII: typographic marks become their plain forms. */
    fun ascii(text: String): String = buildString {
        for (c in text) append(
            when (c) {
                '—', '–', '−' -> '-'
                '‘', '’' -> '\''
                '“', '”' -> '"'
                '•' -> '*'
                '\n' -> '\n'
                else -> if (c.code in 32..126) c else '?'
            },
        )
    }

    /** Splits the Oracle text: the chaos ability (its own paragraph on the card) and the rest. */
    fun text(card: PlaneInfo): AtlasPlaneText {
        val lines = card.text.split('\n').map { it.trim() }.filter { it.isNotEmpty() }
        val chaos = lines.filter { it.contains("{CHAOS}") || it.lowercase().contains("chaos ensues") }
        val rules = lines.filterNot { it in chaos }
        return AtlasPlaneText(
            type = ascii(card.typeLine).take(47),
            artist = ascii(card.artist).take(47),
            text = ascii(rules.joinToString("\n")).take(639),
            chaos = ascii(chaos.joinToString("\n").replace("{CHAOS}", "chaos")).take(319),
        )
    }

    /** Scryfall's art crop, centre-cut to 3:2 and scaled to Atlas's screen, as a baseline JPEG. */
    fun artJpeg(artCrop: ByteArray): ByteArray? {
        val source = BitmapFactory.decodeByteArray(artCrop, 0, artCrop.size) ?: return null
        val owned = mutableListOf(source)
        try {
            val ratio = ART_WIDTH.toFloat() / ART_HEIGHT
            val (w, h) = if (source.width.toFloat() / source.height > ratio) (source.height * ratio).toInt() to source.height
                else source.width to (source.width / ratio).toInt()
            val cut = Bitmap.createBitmap(source, (source.width - w) / 2, (source.height - h) / 2, w, h).also { owned += it }
            val scaled = Bitmap.createScaledBitmap(cut, ART_WIDTH, ART_HEIGHT, true).also { owned += it }
            for (quality in intArrayOf(82, 70, 58, 46, 34)) {
                val out = ByteArrayOutputStream()
                if (scaled.compress(Bitmap.CompressFormat.JPEG, quality, out) && out.size() <= MAX_ART_BYTES) return out.toByteArray()
            }
            return null
        } finally {
            owned.distinct().forEach { it.recycle() }
        }
    }
}
