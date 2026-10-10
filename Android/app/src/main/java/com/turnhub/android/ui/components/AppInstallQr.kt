package com.turnhub.android.ui.components

import android.graphics.Bitmap
import androidx.compose.foundation.Image
import androidx.compose.foundation.layout.size
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.unit.dp
import com.google.zxing.BarcodeFormat
import com.google.zxing.MultiFormatWriter
import com.google.zxing.EncodeHintType

/** Uses the already-shipped ZXing core. No table password or identity in the QR. */
@Composable
fun AppInstallQr() {
    val bitmap = remember {
        val bits = MultiFormatWriter().encode(
            "https://play.google.com/store/apps/details?id=com.turnhub.android",
            BarcodeFormat.QR_CODE, 256, 256, mapOf(EncodeHintType.MARGIN to 4),
        )
        val pixels = IntArray(256 * 256) { i -> if (bits[i % 256, i / 256]) android.graphics.Color.BLACK else android.graphics.Color.WHITE }
        Bitmap.createBitmap(pixels, 256, 256, Bitmap.Config.ARGB_8888).asImageBitmap()
    }
    Image(bitmap, contentDescription = "Scan to install the TurnHub Android app. Then join Atlas Wi-Fi and choose Connect.", modifier = Modifier.size(192.dp))
}
