package com.turnhub.android.ui.tablet

import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.semantics.*
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleEventObserver
import androidx.lifecycle.compose.LocalLifecycleOwner
import com.turnhub.android.standalone.PlanarFace
import kotlinx.coroutines.delay

/** Decoration first, one validated engine roll, then a readable result. */
@Composable
internal fun PlanarRollDialog(reduceMotion: Boolean, onRoll: () -> PlanarFace?, onFinish: (PlanarFace?) -> Unit) {
    var frame by remember { mutableIntStateOf(0) }
    var result by remember { mutableStateOf<PlanarFace?>(null) }
    val roll by rememberUpdatedState(onRoll)
    val finish by rememberUpdatedState(onFinish)
    val owner = LocalLifecycleOwner.current
    DisposableEffect(owner) {
        val observer = LifecycleEventObserver { _, event -> if (event == Lifecycle.Event.ON_STOP) finish(null) }
        owner.lifecycle.addObserver(observer)
        onDispose { owner.lifecycle.removeObserver(observer) }
    }
    LaunchedEffect(Unit) {
        repeat(8) { frame = it; delay(90) }
        if (!owner.lifecycle.currentState.isAtLeast(Lifecycle.State.RESUMED)) { finish(null); return@LaunchedEffect }
        result = roll()
        delay(650)
        finish(result)
    }
    AlertDialog(onDismissRequest = { finish(result) }, title = { Text("Planar die") },
        text = { Text(result?.label ?: if (reduceMotion) "Rolling…" else listOf("⚀", "⚁", "⚂", "⚃", "⚄", "⚅")[frame % 6],
            style = MaterialTheme.typography.displayMedium,
            modifier = Modifier.semantics { liveRegion = LiveRegionMode.Polite; contentDescription = result?.label ?: "Rolling planar die" }) },
        confirmButton = { TextButton({ finish(result) }) { Text(if (result == null) "Cancel" else "Done") } })
}
