package com.turnhub.android.ui.components

import java.net.URLEncoder

/** Public responder link only; opening a report never requires a developer credential. */
const val BUG_REPORT_URL =
    "https://docs.google.com/forms/d/e/1FAIpQLSc0YXkKBOYWhg7RSRTzDLmybXdfsgI0dwg1NzXybKDkNlS04A/viewform"

enum class BugReportMode(val answer: String) {
    ON_DEVICE("Playing on one device"),
    ATLAS("Connected to Atlas"),
}

/** Disposable context from the screen being reported, not the background connection. */
data class BugReportContext(
    val mode: BugReportMode? = null,
    val screen: String? = null,
    val atlasFirmware: String? = null,
    val gameFormat: String? = null,
    val gameState: String? = null,
)

internal data class BugReportDevice(
    val version: String?,
    val manufacturer: String,
    val model: String,
    val androidVersion: String,
    val apiLevel: Int,
    val smallestWidthDp: Int,
    val widthDp: Int,
    val heightDp: Int,
) {
    val type: String? get() = when {
        smallestWidthDp <= 0 -> null
        smallestWidthDp >= 600 -> "Tablet"
        else -> "Phone"
    }
}

/** IDs are the form's question IDs in decimal (Google's entry.<id> URL format). */
internal object BugReportEntries {
    const val MODE = "958949331" // 392867d3
    const val DEVICE_TYPE = "1541494487" // 5be156d7
    const val APP_VERSION = "1579806922" // 5e29f0ca
    const val DEVICE_DETAILS = "61655072" // 03acc820
}

internal fun bugReportDeviceDetails(device: BugReportDevice, report: BugReportContext): String =
    buildList {
        add("Device: ${device.manufacturer} ${device.model}" + device.type?.let { " ($it)" }.orEmpty())
        add("Android: ${device.androidVersion} (API ${device.apiLevel})")
        if (device.widthDp > 0 && device.heightDp > 0) {
            add("Display: ${device.widthDp} x ${device.heightDp} dp; smallest ${device.smallestWidthDp} dp")
        }
        report.screen?.takeIf { it.isNotBlank() }?.let { add("Screen: $it") }
        report.mode?.let { add("Play: ${it.answer}") }
        report.gameFormat?.takeIf { it.isNotBlank() }?.let { add("Game: $it") }
        report.gameState?.takeIf { it.isNotBlank() }?.let { add("State: $it") }
        if (report.mode == BugReportMode.ATLAS) {
            report.atlasFirmware?.takeIf { it.isNotBlank() }?.let { add("Atlas firmware (last known): $it") }
        }
    }.joinToString("\n")

/** Encoded, editable defaults only: never submits a response or attaches a file. */
internal fun bugReportUrl(device: BugReportDevice, report: BugReportContext): String {
    val answers = linkedMapOf(
        "usp" to "pp_url",
        "entry.${BugReportEntries.DEVICE_DETAILS}" to bugReportDeviceDetails(device, report),
    )
    device.type?.let { answers["entry.${BugReportEntries.DEVICE_TYPE}"] = it }
    device.version?.takeIf { it.isNotBlank() }?.let { answers["entry.${BugReportEntries.APP_VERSION}"] = it }
    report.mode?.let { answers["entry.${BugReportEntries.MODE}"] = it.answer }
    return BUG_REPORT_URL + "?" + answers.entries.joinToString("&") { (key, value) ->
        "$key=${URLEncoder.encode(value, "UTF-8")}"
    }
}
