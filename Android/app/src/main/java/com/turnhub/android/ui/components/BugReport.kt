package com.turnhub.android.ui.components

import android.content.ActivityNotFoundException
import android.content.Context
import android.content.Intent
import android.net.Uri
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
import com.turnhub.android.ui.theme.palette

/** Alpha testers' bug report form (a Google Form owned by the TurnHub dev account). */
const val BUG_REPORT_URL =
    "https://docs.google.com/forms/d/e/1FAIpQLSc0YXkKBOYWhg7RSRTzDLmybXdfsgI0dwg1NzXybKDkNlS04A/viewform"

/** Opens the bug report form in the browser. */
fun openBugReport(context: Context) {
    try {
        context.startActivity(Intent(Intent.ACTION_VIEW, Uri.parse(BUG_REPORT_URL)))
    } catch (e: ActivityNotFoundException) {
        Toast.makeText(context, "No browser available. Install or enable a browser to report a bug.", Toast.LENGTH_LONG).show()
    }
}

/** The bug report button, with the version testers should mention. */
@Composable
fun ReportBugButton() {
    val context = LocalContext.current
    Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
        ToneButton("Report a bug", { openBugReport(context) }, Modifier.fillMaxWidth(), tone = Tone.INFO)
        AppVersionLabel()
        Text("${android.os.Build.MODEL}, Android ${android.os.Build.VERSION.RELEASE}",
            color = palette.muted, style = MaterialTheme.typography.bodySmall)
    }
}
