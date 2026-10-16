package com.turnhub.android.ui.components

import android.content.ActivityNotFoundException
import android.content.Context
import android.content.Intent
import android.net.Uri
import android.os.Build
import android.widget.Toast
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import com.turnhub.android.domain.TableSummary
import com.turnhub.android.ui.theme.palette

fun tableBugReportContext(summary: TableSummary, screen: String): BugReportContext = BugReportContext(
    mode = if (summary.atlasId.isBlank()) BugReportMode.ON_DEVICE else BugReportMode.ATLAS,
    screen = screen,
    atlasFirmware = summary.firmwareVersion.takeIf { it.isNotBlank() },
    gameFormat = summary.settings.profile.wireValue,
    gameState = summary.state.name,
)

private fun bugReportDevice(context: Context): BugReportDevice {
    val configuration = context.resources.configuration
    return BugReportDevice(
        version = runCatching { installedAppVersion(context) }.getOrNull(),
        manufacturer = Build.MANUFACTURER,
        model = Build.MODEL,
        androidVersion = Build.VERSION.RELEASE,
        apiLevel = Build.VERSION.SDK_INT,
        smallestWidthDp = configuration.smallestScreenWidthDp,
        widthDp = configuration.screenWidthDp,
        heightDp = configuration.screenHeightDp,
    )
}

/** Opens editable defaults in the browser; the user reviews and submits the report. */
fun openBugReport(context: Context, report: BugReportContext = BugReportContext()) {
    val uri = Uri.parse(bugReportUrl(bugReportDevice(context), report))
    try {
        context.startActivity(Intent(Intent.ACTION_VIEW, uri).apply {
            if (context !is android.app.Activity) addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
        })
    } catch (e: ActivityNotFoundException) {
        Toast.makeText(context, "No browser available. Install or enable a browser to report a bug.", Toast.LENGTH_LONG).show()
    }
}

/** The bug report button, with a visible summary of the prefilled version/device. */
@Composable
fun ReportBugButton(report: BugReportContext = BugReportContext()) {
    val context = LocalContext.current
    Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
        ToneButton("Report a bug", { openBugReport(context, report) }, Modifier.fillMaxWidth(), tone = Tone.INFO)
        AppVersionLabel()
        Text("${android.os.Build.MODEL}, Android ${android.os.Build.VERSION.RELEASE}",
            color = palette.muted, style = MaterialTheme.typography.bodySmall)
    }
}

/** Explains the prefilled diagnostics before the deliberate handoff to Google Forms. */
@Composable
fun BugReportDialog(onDismiss: () -> Unit, report: BugReportContext = BugReportContext()) {
    val context = LocalContext.current
    androidx.compose.material3.AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text("Report a bug") },
        text = {
            Column(verticalArrangement = Arrangement.spacedBy(6.dp)) {
                Text("Opens the Google bug form with version and device details filled in. Review the details before submitting.")
                AppVersionLabel()
                Text("${android.os.Build.MODEL}, Android ${android.os.Build.VERSION.RELEASE}",
                    color = palette.muted, style = MaterialTheme.typography.bodySmall)
            }
        },
        confirmButton = { androidx.compose.material3.TextButton({ onDismiss(); openBugReport(context, report) }) { Text("Open form") } },
        dismissButton = { androidx.compose.material3.TextButton(onDismiss) { Text("Cancel") } },
    )
}
