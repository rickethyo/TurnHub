package com.turnhub.android.protocol

/** A vector preset keyed by Atlas, with historical bitmap rows as fallback,
 * or an approved uploaded image attached by the presentation repository.
 * Artwork bytes never enter authoritative game state.
 */
data class AvatarIcon(
    val id: Int,
    val key: String,
    val label: String,
    val rows: List<String>,
    val image: ByteArray? = null,
) {
    fun ink(x: Int, y: Int): Boolean = rows.getOrNull(y)?.getOrNull(x) == '#'
}
