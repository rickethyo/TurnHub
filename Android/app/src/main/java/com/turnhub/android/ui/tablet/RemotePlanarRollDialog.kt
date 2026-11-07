package com.turnhub.android.ui.tablet

import com.turnhub.android.ui.components.ActionRole

import com.turnhub.android.ui.components.ActionButton

import androidx.compose.foundation.layout.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.semantics.*
import androidx.compose.ui.unit.dp
import com.turnhub.android.lan.PlanarRollFeedback
import com.turnhub.android.standalone.PlanarFace
import kotlinx.coroutines.delay

/** The animation is decorative; only the matching action receipt supplies the die face. */
@Composable
internal fun RemotePlanarRollDialog(actionId: String, feedback: PlanarRollFeedback?, reduceMotion: Boolean,
    canRetry: Boolean, physical: Boolean, onRetry: () -> Unit, onClose: () -> Unit, onShowPlanes: () -> Unit) {
    val result = feedback?.takeIf { it.actionId == actionId }
    val latest by rememberUpdatedState(result)
    var frame by remember(actionId) { mutableIntStateOf(0) }
    var introDone by remember(actionId) { mutableStateOf(reduceMotion) }
    LaunchedEffect(actionId, reduceMotion) {
        if (reduceMotion) { introDone = true; return@LaunchedEffect }
        repeat(8) { frame++; delay(75) }
        introDone = true
        while (latest?.status == "pending") { frame++; delay(90) }
    }
    val waiting = result?.status == "pending" || !introDone
    val face = result?.face.takeUnless { waiting }
    val message = when {
        waiting -> "Waiting for the host to confirm this roll…"
        face == PlanarFace.BLANK -> "Blank. No planar effect."
        face == PlanarFace.CHAOS -> "Chaos ensues! Resolve the plane's chaos ability."
        face == PlanarFace.PLANESWALK -> if (physical) "Planeswalk! Turn over the next plane." else "Planeswalk! The host advanced to the next plane."
        result?.status == "accepted" -> "Roll accepted. Open Planes to check its result."
        result?.status == "blocked" -> "The roll wasn't sent. Check your active turn and connection."
        result?.status in setOf("stale", "rejected", "forbidden", "renew") -> "Roll not applied. Your turn or the table state changed."
        else -> "The result is not confirmed. Check the same roll before rolling again."
    }
    AlertDialog(onDismissRequest = onClose, title = { Text("Planar die") }, text = {
        Column(verticalArrangement = Arrangement.spacedBy(12.dp), modifier = Modifier.semantics { liveRegion = LiveRegionMode.Polite }) {
            Text(if (waiting) if (reduceMotion) "Rolling…" else listOf("⚀", "⚁", "⚂", "⚃", "⚄", "⚅")[frame % 6]
                else face?.label ?: "Roll status", style = MaterialTheme.typography.displaySmall,
                modifier = Modifier.semantics { contentDescription = if (waiting) "Rolling planar die" else face?.label ?: "Roll status" })
            Text(message)
        }
    }, confirmButton = {
        when {
            !waiting && (face != null && face != PlanarFace.BLANK || result?.status == "accepted") -> ActionButton(onShowPlanes, role = ActionRole.PRIMARY) { Text("Show planes") }
            !waiting && canRetry && result?.status == "uncertain" -> ActionButton(onRetry, role = ActionRole.PRIMARY) { Text("Check same roll") }
            else -> ActionButton(onClose, role = ActionRole.PRIMARY) { Text(if (waiting) "Close" else "Done") }
        }
    }, dismissButton = {
        if (!waiting && (face != null && face != PlanarFace.BLANK || canRetry)) ActionButton(onClose, role = ActionRole.SECONDARY) { Text("Done") }
    })
}
