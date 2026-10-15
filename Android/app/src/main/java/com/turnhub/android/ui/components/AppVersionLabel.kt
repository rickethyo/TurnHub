package com.turnhub.android.ui.components

import android.content.pm.ApplicationInfo
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.core.content.pm.PackageInfoCompat
import com.turnhub.android.ui.theme.palette

/** Read the installed package so Play's build number and local overrides stay accurate. */
@Composable
fun AppVersionLabel(modifier: Modifier = Modifier) {
    val context = LocalContext.current
    val label = remember(context) {
        val info = context.packageManager.getPackageInfo(context.packageName, 0)
        val debug = context.applicationInfo.flags and ApplicationInfo.FLAG_DEBUGGABLE != 0
        "TurnHub ${info.versionName} · Build ${PackageInfoCompat.getLongVersionCode(info)}" +
            if (debug) " · Debug" else ""
    }
    Text(label, modifier = modifier, color = palette.muted, style = MaterialTheme.typography.bodySmall)
}
