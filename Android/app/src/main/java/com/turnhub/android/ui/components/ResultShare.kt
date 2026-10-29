package com.turnhub.android.ui.components

import android.content.ClipData
import android.content.Context
import android.content.Intent
import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Paint
import android.graphics.Typeface
import androidx.core.content.FileProvider
import java.io.File
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

/**
 * Shares a finished game: a picture of the final table under a banner with
 * the result, plus [text] (the game overview) for apps that take a caption.
 * The banner shows the first two lines of [text].
 */
suspend fun shareGameResult(context: Context, table: Bitmap, text: String, background: Int, title: Int, detail: Int) {
    val file = withContext(Dispatchers.Default) {
        val image = withBanner(table, text.lines().take(2), background, title, detail)
        withContext(Dispatchers.IO) {
            val dir = File(context.cacheDir, "shared").apply { mkdirs() }
            File(dir, "turnhub-result.png").also { out -> out.outputStream().use { image.compress(Bitmap.CompressFormat.PNG, 100, it) } }
        }
    }
    val uri = FileProvider.getUriForFile(context, "${context.packageName}.share", file)
    val send = Intent(Intent.ACTION_SEND).apply {
        type = "image/png"
        putExtra(Intent.EXTRA_STREAM, uri)
        putExtra(Intent.EXTRA_TEXT, text)
        putExtra(Intent.EXTRA_SUBJECT, text.lineSequence().firstOrNull().orEmpty())
        // The chooser previews the picture too.
        clipData = ClipData.newRawUri(null, uri)
        addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
    }
    context.startActivity(Intent.createChooser(send, "Share result"))
}

private fun withBanner(table: Bitmap, lines: List<String>, background: Int, title: Int, detail: Int): Bitmap {
    // A captured layer can be a hardware bitmap, which a software canvas can't draw.
    val source = if (table.config == Bitmap.Config.HARDWARE) table.copy(Bitmap.Config.ARGB_8888, false) else table
    val width = source.width
    val titleSize = width / 22f
    val detailSize = width / 40f
    val pad = width / 30f
    val bannerHeight = (pad * 2 + titleSize + detailSize * 1.6f).toInt()
    val out = Bitmap.createBitmap(width, source.height + bannerHeight, Bitmap.Config.ARGB_8888)
    Canvas(out).apply {
        drawColor(background)
        val titlePaint = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = title; textSize = titleSize; typeface = Typeface.DEFAULT_BOLD }
        val detailPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = detail; textSize = detailSize }
        // Long names shrink to fit the width rather than run off the edge.
        fun Paint.fit(line: String) = apply { textSize = minOf(textSize, textSize * (width - 2 * pad) / measureText(line).coerceAtLeast(1f)) }
        lines.getOrNull(0)?.let { drawText(it, pad, pad + titleSize, titlePaint.fit(it)) }
        lines.getOrNull(1)?.let { drawText(it, pad, pad + titleSize + detailSize * 1.4f, detailPaint.fit(it)) }
        drawBitmap(source, 0f, bannerHeight.toFloat(), null)
    }
    return out
}

/** Shares match records as a CSV file ([MatchCsv][com.turnhub.android.standalone.MatchCsv]) for a spreadsheet. */
suspend fun shareCsv(context: Context, fileName: String, csv: String) {
    val file = withContext(Dispatchers.IO) {
        val dir = File(context.cacheDir, "shared").apply { mkdirs() }
        File(dir, fileName).also { it.writeText(csv) }
    }
    val uri = FileProvider.getUriForFile(context, "${context.packageName}.share", file)
    val send = Intent(Intent.ACTION_SEND).apply {
        type = "text/csv"
        putExtra(Intent.EXTRA_STREAM, uri)
        putExtra(Intent.EXTRA_SUBJECT, fileName)
        clipData = ClipData.newRawUri(null, uri)
        addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
    }
    context.startActivity(Intent.createChooser(send, "Share CSV"))
}
