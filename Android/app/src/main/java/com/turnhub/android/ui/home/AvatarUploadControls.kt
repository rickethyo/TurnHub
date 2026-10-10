package com.turnhub.android.ui.home

import android.content.Context
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.graphics.Matrix
import android.media.ExifInterface
import android.net.Uri
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.Image
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.unit.IntSize
import androidx.compose.ui.unit.dp
import com.turnhub.android.data.Personalization
import com.turnhub.android.ui.components.ToneButton
import com.turnhub.android.ui.theme.palette
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.io.ByteArrayOutputStream
import java.io.File

/** Phone-local decode/crop. Only the bounded compressed square reaches Atlas. */
private suspend fun loadSource(context: Context, uri: Uri): Bitmap = withContext(Dispatchers.IO) {
    val file = File.createTempFile("avatar-crop-", ".image", context.cacheDir)
    try {
        context.contentResolver.openInputStream(uri).use { input ->
            requireNotNull(input) { "This image could not be opened." }
            file.outputStream().use { output ->
                val buffer = ByteArray(8192)
                var total = 0
                while (true) {
                    val n = input.read(buffer)
                    if (n < 0) break
                    total += n
                    require(total <= 20 * 1024 * 1024) { "Choose an image smaller than 20 MiB." }
                    output.write(buffer, 0, n)
                }
            }
        }
        val bounds = BitmapFactory.Options().apply { inJustDecodeBounds = true }
        BitmapFactory.decodeFile(file.path, bounds)
        require(bounds.outWidth > 0 && bounds.outHeight > 0) { "Choose a JPEG, PNG or WebP image." }
        require(bounds.outWidth.toLong() * bounds.outHeight <= 32_000_000L) { "Choose an image up to 32 megapixels." }
        val options = BitmapFactory.Options().apply {
            inSampleSize = 1
            while (maxOf(bounds.outWidth, bounds.outHeight) / inSampleSize > 2048) inSampleSize *= 2
        }
        val decoded = requireNotNull(BitmapFactory.decodeFile(file.path, options)) { "This image could not be decoded." }
        val orientation = try {
            ExifInterface(file.path).getAttributeInt(ExifInterface.TAG_ORIENTATION, ExifInterface.ORIENTATION_NORMAL)
        } catch (_: Exception) { ExifInterface.ORIENTATION_NORMAL }
        val matrix = Matrix().apply {
            when (orientation) {
                ExifInterface.ORIENTATION_FLIP_HORIZONTAL -> setScale(-1f, 1f)
                ExifInterface.ORIENTATION_ROTATE_180 -> setRotate(180f)
                ExifInterface.ORIENTATION_FLIP_VERTICAL -> setScale(1f, -1f)
                ExifInterface.ORIENTATION_TRANSPOSE -> { setRotate(90f); postScale(-1f, 1f) }
                ExifInterface.ORIENTATION_ROTATE_90 -> setRotate(90f)
                ExifInterface.ORIENTATION_TRANSVERSE -> { setRotate(-90f); postScale(-1f, 1f) }
                ExifInterface.ORIENTATION_ROTATE_270 -> setRotate(-90f)
            }
        }
        Bitmap.createBitmap(decoded, 0, 0, decoded.width, decoded.height, matrix, true).also {
            if (it !== decoded) decoded.recycle()
        }
    } finally { file.delete() }
}

private suspend fun compressCrop(source: Bitmap, zoom: Float, horizontal: Float, vertical: Float): ByteArray =
    withContext(Dispatchers.Default) {
        val side = (minOf(source.width, source.height) / zoom).toInt().coerceAtLeast(1)
        val crop = Bitmap.createBitmap(source, ((source.width - side) * horizontal).toInt(),
            ((source.height - side) * vertical).toInt(), side, side)
        // Flatten transparency explicitly: JPEG is RGB on every decoder.
        val output = Bitmap.createBitmap(512, 512, Bitmap.Config.ARGB_8888)
        android.graphics.Canvas(output).apply {
            drawColor(android.graphics.Color.WHITE)
            drawBitmap(crop, null, android.graphics.Rect(0, 0, 512, 512), android.graphics.Paint(android.graphics.Paint.FILTER_BITMAP_FLAG))
        }
        try {
            for (quality in listOf(90, 80, 70, 60, 50, 40, 30, 20)) {
                val bytes = ByteArrayOutputStream().also { output.compress(Bitmap.CompressFormat.JPEG, quality, it) }.toByteArray()
                if (bytes.size <= 48 * 1024) return@withContext bytes
            }
            error("This crop has too much detail. Choose a simpler crop.")
        } finally {
            output.recycle()
            if (crop !== source) crop.recycle()
        }
    }

@Composable
fun AvatarUploadControls(current: Personalization?, busy: Boolean, upload: (ByteArray, String) -> Unit, remove: () -> Unit) {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    var source by remember { mutableStateOf<Bitmap?>(null) }
    var loading by remember { mutableStateOf(false) }
    var error by remember { mutableStateOf<String?>(null) }
    val picker = rememberLauncherForActivityResult(ActivityResultContracts.GetContent()) { uri ->
        if (uri != null) scope.launch {
            loading = true; error = null
            try { source = loadSource(context, uri) }
            catch (e: CancellationException) { throw e }
            catch (e: Exception) { error = e.message ?: "This image could not be opened." }
            finally { loading = false }
        }
    }
    ToneButton(if (loading) "Opening image…" else "Upload your own image", { picker.launch("image/*") },
        enabled = current?.cardPresent == true && !busy && !loading)
    Text("Crop a square image. Saved at up to 512 × 512 pixels and 48 KiB. An Admin approves images before public display.",
        color = palette.muted, style = MaterialTheme.typography.bodySmall)
    error?.let { Text(it, color = palette.warn) }
    if (current?.pendingAvatar?.isNotBlank() == true) {
        Text("Awaiting Admin approval", color = palette.warn)
        AvatarBytesPreview(current.pendingImage, "Image awaiting approval")
    } else if (current?.customAvatar?.isNotBlank() == true) Text("Uploaded image approved", color = palette.muted)
    if (current?.pendingAvatar?.isNotBlank() == true || current?.customAvatar?.isNotBlank() == true)
        ToneButton("Remove uploaded image", remove, enabled = !busy)
    source?.let { bitmap ->
        AvatarCropDialog(bitmap, onDismiss = { source = null }, onUpload = { bytes -> source = null
            val decoded = BitmapFactory.decodeByteArray(bytes, 0, bytes.size)
            val thumb = Bitmap.createScaledBitmap(decoded, 16, 16, true)
            val pixels = IntArray(256); thumb.getPixels(pixels, 0, 16, 0, 0, 16, 16)
            val hex = pixels.joinToString("") { color ->
                val pixel = ((color shr 16 and 255) shr 5 shl 5) or ((color shr 8 and 255) shr 5 shl 2) or ((color and 255) shr 6)
                "%02x".format(pixel)
            }
            if (thumb !== decoded) thumb.recycle()
            decoded.recycle()
            upload(bytes, hex) })
    }
}

@Composable
fun AvatarBytesPreview(bytes: ByteArray?, description: String) {
    val bitmap = remember(bytes) {
        bytes?.let {
            val options = BitmapFactory.Options().apply { inJustDecodeBounds = true }
            BitmapFactory.decodeByteArray(it, 0, it.size, options)
            if (options.outWidth in 32..512 && options.outHeight == options.outWidth)
                BitmapFactory.decodeByteArray(it, 0, it.size) else null
        }
    }
    if (bitmap != null) Image(bitmap.asImageBitmap(), description, Modifier.size(128.dp))
    else Text("Image preview unavailable", color = palette.muted)
}

@Composable
private fun AvatarCropDialog(source: Bitmap, onDismiss: () -> Unit, onUpload: (ByteArray) -> Unit) {
    var zoom by remember { mutableFloatStateOf(1f) }
    var horizontal by remember { mutableFloatStateOf(.5f) }
    var vertical by remember { mutableFloatStateOf(.5f) }
    var encoded by remember { mutableStateOf<ByteArray?>(null) }
    var saving by remember { mutableStateOf(false) }
    var error by remember { mutableStateOf<String?>(null) }
    val scope = rememberCoroutineScope()
    val image = remember(source) { source.asImageBitmap() }
    fun changed() { encoded = null; error = null }
    AlertDialog(
        onDismissRequest = { if (!saving) onDismiss() },
        title = { Text("Crop player image") },
        text = {
            Column(Modifier.verticalScroll(rememberScrollState()), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                val side = (minOf(source.width, source.height) / zoom).toInt().coerceAtLeast(1)
                Canvas(Modifier.fillMaxWidth().aspectRatio(1f).semantics { contentDescription = "Square crop preview" }) {
                    drawImage(image, IntOffset(((source.width - side) * horizontal).toInt(), ((source.height - side) * vertical).toInt()),
                        IntSize(side, side), dstSize = IntSize(size.width.toInt(), size.height.toInt()))
                }
                Text("Zoom")
                Slider(zoom, { zoom = it; changed() }, enabled = !saving, valueRange = 1f..4f, modifier = Modifier.semantics { contentDescription = "Crop zoom" })
                Text("Horizontal position")
                Slider(horizontal, { horizontal = it; changed() }, enabled = !saving, modifier = Modifier.semantics { contentDescription = "Horizontal crop position" })
                Text("Vertical position")
                Slider(vertical, { vertical = it; changed() }, enabled = !saving, modifier = Modifier.semantics { contentDescription = "Vertical crop position" })
                encoded?.let { bytes ->
                    Text("Compressed preview · ${bytes.size / 1024} KiB")
                    AvatarBytesPreview(bytes, "Compressed crop")
                    Row(horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                        DeviceCropPreview(bytes, 64, "2.8-inch · 64 px")
                        DeviceCropPreview(bytes, 128, "4-inch · 128 px")
                    }
                    Text("Display previews use provisional icon sizes; final LCD layouts may differ.", style = MaterialTheme.typography.bodySmall)
                }
                error?.let { Text(it, color = MaterialTheme.colorScheme.error) }
            }
        },
        confirmButton = {
            TextButton(enabled = !saving, onClick = {
                val ready = encoded
                if (ready != null) onUpload(ready)
                else scope.launch {
                    saving = true
                    try { encoded = compressCrop(source, zoom, horizontal, vertical) }
                    catch (e: CancellationException) { throw e }
                    catch (e: Exception) { error = e.message ?: "Could not compress crop" }
                    finally { saving = false }
                }
            }) { Text(if (saving) "Compressing…" else if (encoded == null) "Preview compressed image" else "Upload for approval") }
        },
        dismissButton = { TextButton(onClick = onDismiss, enabled = !saving) { Text("Cancel") } },
    )
}

@Composable
private fun DeviceCropPreview(bytes: ByteArray, side: Int, label: String) {
    val bitmap = remember(bytes, side) {
        val decoded = BitmapFactory.decodeByteArray(bytes, 0, bytes.size)
        Bitmap.createScaledBitmap(decoded, side, side, true).also { if (it !== decoded) decoded.recycle() }
    }
    Column {
        Image(bitmap.asImageBitmap(), label, Modifier.size(64.dp))
        Text(label, style = MaterialTheme.typography.labelSmall)
    }
}
