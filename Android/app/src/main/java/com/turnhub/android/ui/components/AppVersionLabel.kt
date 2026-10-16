package com.turnhub.android.ui.components

import android.content.Context
import android.content.pm.ApplicationInfo
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.core.content.pm.PackageInfoCompat
import com.turnhub.android.ui.theme.palette

/** Shared by the visible version label and bug-report defaults; never a source-code version. */
internal fun installedAppVersion(context: Context): String {
    val info = context.packageManager.getPackageInfo(context.packageName, 0)
    val debug = context.applicationInfo.flags and ApplicationInfo.FLAG_DEBUGGABLE != 0
    return "${info.versionName} · Build ${PackageInfoCompat.getLongVersionCode(info)}" +
        if (debug) " · Debug" else ""
}

/** Read the installed package so Play's build number and local overrides stay accurate. */
@Composable
fun AppVersionLabel(modifier: Modifier = Modifier) {
    val context = LocalContext.current
    val label = remember(context) {
        "TurnHub ${installedAppVersion(context)}"
    }
    Text(label, modifier = modifier, color = palette.muted, style = MaterialTheme.typography.bodySmall)
}
