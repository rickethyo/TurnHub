package com.turnhub.android.ui.tablet

import android.content.Context
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateMapOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.platform.LocalContext
import androidx.core.content.pm.PackageInfoCompat
import com.turnhub.android.data.ArtCrop
import com.turnhub.android.data.ScryfallCommanderArt
import com.turnhub.android.data.TileCardGame
import java.io.File

/** A commander's art, decoded for a tile, with the artist credit shown beside it. */
internal class TileArt(val image: ImageBitmap, val cardName: String, val artist: String, val rarity: String? = null, val source: String = "Scryfall")

private const val ART_PREF = "commander_art"

/** One lookup service and one decoded copy per name for the whole app. */
internal object CommanderArtCache {
    var source: ScryfallCommanderArt? = null
    val decoded = mutableStateMapOf<String, TileArt>()

    fun source(context: Context): ScryfallCommanderArt = source ?: run {
        val app = context.applicationContext
        ScryfallCommanderArt(File(app.cacheDir, "commander_art"), scryfallUserAgent(app)).also { source = it }
    }
}

/** The User-Agent Scryfall asks every app to send: the app's name and version (commander art and planes). */
internal fun scryfallUserAgent(app: Context): String {
    val version = runCatching { app.packageManager.getPackageInfo(app.packageName, 0) }.getOrNull()
    return "TurnHub/${version?.versionName ?: "0"} (Android; build ${version?.let { PackageInfoCompat.getLongVersionCode(it) } ?: 0})"
}

/** Whether named commanders show their card art (Settings; on unless turned off). */
@Composable
internal fun rememberCommanderArtEnabled(): Pair<Boolean, (Boolean) -> Unit> {
    val context = LocalContext.current
    val prefs = remember(context) { context.getSharedPreferences("turnhub_ui", Context.MODE_PRIVATE) }
    var on by remember { mutableStateOf(prefs.getBoolean(ART_PREF, true)) }
    return on to { value: Boolean -> on = value; prefs.edit().putBoolean(ART_PREF, value).apply() }
}

/** The art window of a full card image ([crop]); the image itself when it is already art. */
internal fun cropArt(bitmap: Bitmap, crop: ArtCrop?): Bitmap {
    crop ?: return bitmap
    val left = (bitmap.width * crop.left).toInt().coerceIn(0, bitmap.width - 1)
    val top = (bitmap.height * crop.top).toInt().coerceIn(0, bitmap.height - 1)
    val right = (bitmap.width * crop.right).toInt().coerceIn(left + 1, bitmap.width)
    val bottom = (bitmap.height * crop.bottom).toInt().coerceIn(top + 1, bitmap.height)
    return Bitmap.createBitmap(bitmap, left, top, right - left, bottom - top)
}

/** The art for a named commander, once Scryfall (or the cache) has it; null otherwise. */
@Composable
internal fun rememberCommanderArt(name: String?, printing: String? = null): TileArt? {
    val context = LocalContext.current
    val enabled = rememberCommanderArtEnabled().first
    if (name.isNullOrBlank() || !enabled || printing == TileCardGame.NO_ART) return null
    val key = ScryfallCommanderArt.cacheKey(if (printing == null) name else "$name#$printing")
    // Keyed state, not produceState: its value would keep the previous card's art (and name)
    // until the new card's art finished downloading.
    val art = remember(key) { mutableStateOf(CommanderArtCache.decoded[key]) }
    LaunchedEffect(key) {
        if (art.value != null) return@LaunchedEffect
        val loaded = CommanderArtCache.source(context).art(name, printing) ?: return@LaunchedEffect
        // Lorcana images are AVIF, which Android decodes from version 12; older phones show no art.
        val bitmap = BitmapFactory.decodeByteArray(loaded.jpeg, 0, loaded.jpeg.size)
            ?.let { cropArt(it, TileCardGame.of(printing).crop) } ?: return@LaunchedEffect
        val tile = TileArt(bitmap.asImageBitmap(), loaded.cardName, loaded.artist, loaded.rarity, TileCardGame.of(printing).source)
        CommanderArtCache.decoded[key] = tile
        art.value = tile
    }
    return art.value
}

/**
 * Death May Die: the investigator's picture for their tile, once the wiki (or the cache) has
 * it; null for no investigator, one the wiki has no picture of, or card art turned off.
 */
@Composable
internal fun rememberInvestigatorArt(name: String?): TileArt? {
    val context = LocalContext.current
    val enabled = rememberCommanderArtEnabled().first
    if (name.isNullOrBlank() || !enabled || com.turnhub.android.domain.DeathMayDie.artUrl(name) == null) return null
    val key = "investigator#$name"
    val art = remember(key) { mutableStateOf(CommanderArtCache.decoded[key]) }
    LaunchedEffect(key) {
        if (art.value != null) return@LaunchedEffect
        val bytes = CommanderArtCache.source(context).investigator(name) ?: return@LaunchedEffect
        val bitmap = BitmapFactory.decodeByteArray(bytes, 0, bytes.size) ?: return@LaunchedEffect
        // A dashboard picture is wide; its portrait is the left-hand panel.
        val crop = com.turnhub.android.domain.DeathMayDie.dashboardPortrait.takeIf { bitmap.width > bitmap.height * 13 / 10 }
            ?.let { (l, t, r, b) -> ArtCrop(l, t, r, b) }
        val tile = TileArt(cropArt(bitmap, crop).asImageBitmap(), name, "CMON", source = "the Death May Die wiki")
        CommanderArtCache.decoded[key] = tile
        art.value = tile
    }
    return art.value
}
