package com.turnhub.android.ui.tablet

import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.semantics.*
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleEventObserver
import androidx.lifecycle.compose.LocalLifecycleOwner
import com.turnhub.android.protocol.TableState
import kotlinx.coroutines.delay

/** A local pre-start presentation. No engine action or clock starts until it finishes. */
@Composable
internal fun rememberGameStartCountdown(state: TableState, enabled: Boolean = true, onStart: () -> Unit): () -> Unit {
    var active by remember { mutableStateOf(false) }
    var expected by remember { mutableStateOf(state) }
    var seconds by remember { mutableIntStateOf(3) }
    val start by rememberUpdatedState(onStart)
    val owner = LocalLifecycleOwner.current
    DisposableEffect(owner) {
        val observer = LifecycleEventObserver { _, event -> if (event == Lifecycle.Event.ON_STOP) active = false }
        owner.lifecycle.addObserver(observer)
        onDispose { owner.lifecycle.removeObserver(observer) }
    }
    LaunchedEffect(active, state, enabled) {
        if (!active) return@LaunchedEffect
        if (!enabled || state != expected) { active = false; return@LaunchedEffect }
        for (n in 3 downTo 1) { seconds = n; delay(1000) }
        if (active && owner.lifecycle.currentState.isAtLeast(Lifecycle.State.RESUMED)) {
            active = false
            start()
        } else active = false
    }
    if (active) AlertDialog(
        onDismissRequest = { active = false },
        title = { Text("Get ready") },
        text = { Text("$seconds", style = MaterialTheme.typography.displayLarge,
            modifier = Modifier.semantics { liveRegion = LiveRegionMode.Assertive; contentDescription = "Game starts in $seconds" }) },
        confirmButton = { TextButton({ active = false }) { Text("Cancel") } },
    )
    return { if (!active && enabled && state in listOf(TableState.LOBBY, TableState.GAME_OVER)) {
        expected = state; seconds = 3; active = true
    } }
}
