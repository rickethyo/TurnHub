package com.turnhub.android.protocol

/**
 * One preset avatar from `GET /api/avatars`: the same 16x16 one-bit icon Atlas's
 * screen and the Sigils draw (shared/include/avatars.h). Each of [rows] is
 * [size] characters, `#` ink and `.` background.
 *
 * Seats carry the preset's [id] (`avatar` in `/api/seats`; 0 = none). Custom
 * avatars are planned for signed-in viewers only and never appear there.
 */
data class AvatarIcon(
    val id: Int,
    val key: String,
    val label: String,
    val rows: List<String>,
) {
    fun ink(x: Int, y: Int): Boolean = rows.getOrNull(y)?.getOrNull(x) == '#'
}
