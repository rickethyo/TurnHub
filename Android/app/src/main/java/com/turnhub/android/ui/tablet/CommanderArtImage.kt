package com.turnhub.android.ui.tablet

import android.content.Context
import android.graphics.BitmapFactory
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateMapOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.produceState
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.platform.LocalContext
import androidx.core.content.pm.PackageInfoCompat
import com.turnhub.android.data.ScryfallCommanderArt
import java.io.File

/** A commander's art, decoded for a tile, with the artist credit shown beside it. */
internal class TileArt(val image: ImageBitmap, val cardName: String, val artist: String)

private const val ART_PREF = "commander_art"

/** One lookup service and one decoded copy per name for the whole app. */
private object CommanderArtCache {
    var source: ScryfallCommanderArt? = null
    val decoded = mutableStateMapOf<String, TileArt>()

    fun source(context: Context): ScryfallCommanderArt = source ?: run {
        val app = context.applicationContext
        ScryfallCommanderArt(File(app.cacheDir, "commander_art"), scryfallUserAgent(app)).also { source = it }
    }
}

/** The User-Agent Scryfall asks every app to send: the app's name and version. */
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

/** The art for a named commander, once Scryfall (or the cache) has it; null otherwise. */
@Composable
internal fun rememberCommanderArt(name: String?): TileArt? {
    val context = LocalContext.current
    val enabled = rememberCommanderArtEnabled().first
    if (name.isNullOrBlank() || !enabled) return null
    val key = ScryfallCommanderArt.cacheKey(name)
    val art by produceState(CommanderArtCache.decoded[key], key) {
        if (value != null) return@produceState
        val loaded = CommanderArtCache.source(context).art(name) ?: return@produceState
        val bitmap = BitmapFactory.decodeByteArray(loaded.jpeg, 0, loaded.jpeg.size) ?: return@produceState
        val tile = TileArt(bitmap.asImageBitmap(), loaded.cardName, loaded.artist)
        CommanderArtCache.decoded[key] = tile
        value = tile
    }
    return art
}
