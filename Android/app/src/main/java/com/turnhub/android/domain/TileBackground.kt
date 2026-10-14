package com.turnhub.android.domain

import java.util.Base64

/** Player-owned presentation, separate from avatars and authoritative match data. */
enum class TileBackgroundStyle(val key: String, val label: String) {
    DEFAULT("default", "Default"),
    SOLID("solid", "Solid"),
    GRADIENT("gradient", "Gradient"),
    IMAGE("image", "Image"),
}

data class TileBackground(
    val style: TileBackgroundStyle = TileBackgroundStyle.DEFAULT,
    val color: String? = null,
    /** Bounded, phone-cropped image; retained when temporarily choosing another style. */
    val image: String? = null,
) {
    init {
        require(color == null || color.matches(Regex("#[0-9a-fA-F]{6}")))
        require(image == null || Base64.getDecoder().decode(image).size in 1..MAX_IMAGE_BYTES)
        require(style != TileBackgroundStyle.IMAGE || image != null)
    }

    companion object {
        const val MAX_IMAGE_BYTES = 48 * 1024
    }
}
