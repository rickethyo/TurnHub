package com.turnhub.android.ui.components

import androidx.compose.foundation.layout.*
import androidx.compose.foundation.selection.toggleable
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Slider
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.dp
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleEventObserver
import androidx.lifecycle.compose.LocalLifecycleOwner
import com.turnhub.android.data.GameSoundPlayer
import com.turnhub.android.domain.GameSoundEvents
import com.turnhub.android.domain.GameSoundPreferences
import com.turnhub.android.domain.GameSoundSnapshot
import com.turnhub.android.domain.TableSummary
import com.turnhub.android.ui.theme.palette

private data class GameAudio(
    val preferences: GameSoundPreferences = GameSoundPreferences(),
    val onChange: (GameSoundPreferences) -> Unit = {},
    val foreground: Boolean = false,
    val events: GameSoundEvents = GameSoundEvents(),
    val player: GameSoundPlayer? = null,
)
private val LocalGameAudio = staticCompositionLocalOf { GameAudio() }

@Composable
fun ProvideGameSounds(preferences: GameSoundPreferences, onChange: (GameSoundPreferences) -> Unit,
    content: @Composable () -> Unit) {
    val context = LocalContext.current.applicationContext
    val lifecycle = LocalLifecycleOwner.current.lifecycle
    val events = remember { GameSoundEvents() }
    val player = remember(context) { runCatching { GameSoundPlayer(context) }.getOrNull() }
    var foreground by remember(lifecycle) { mutableStateOf(lifecycle.currentState.isAtLeast(Lifecycle.State.RESUMED)) }
    DisposableEffect(lifecycle, player) {
        player?.setForeground(foreground)
        val observer = LifecycleEventObserver { _, _ ->
            foreground = lifecycle.currentState.isAtLeast(Lifecycle.State.RESUMED)
            player?.setForeground(foreground)
            if (!foreground) { events.reset(); player?.stop() }
        }
        lifecycle.addObserver(observer)
        onDispose { lifecycle.removeObserver(observer); events.reset(); player?.setForeground(false); player?.release() }
    }
    // Turning sound/volume off also cuts off an already playing cue.
    LaunchedEffect(preferences) { if (!preferences.enabled || preferences.volume == 0) player?.stop() }
    CompositionLocalProvider(LocalGameAudio provides GameAudio(preferences, onChange, foreground, events, player), content = content)
}

/** One mounted observer per visible local/connected screen, across lobby/game transitions. */
@Composable
fun GameSoundEffect(summary: TableSummary?, available: Boolean = true) {
    val audio = LocalGameAudio.current
    val snapshot = summary?.let { GameSoundSnapshot.from(it) }
    DisposableEffect(audio.events, audio.player) {
        onDispose { audio.events.reset(); audio.player?.stop() }
    }
    LaunchedEffect(snapshot, available, audio.foreground, audio.preferences) {
        val enabled = available && audio.foreground && audio.preferences.enabled && audio.preferences.volume > 0
        audio.events.observe(snapshot, enabled)?.let { audio.player?.play(it, audio.preferences.volume) }
    }
}

@Composable
fun GameSoundControls() {
    val audio = LocalGameAudio.current
    val prefs = audio.preferences
    Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
        Row(Modifier.fillMaxWidth().toggleable(prefs.enabled, role = Role.Switch,
            onValueChange = { audio.onChange(prefs.copy(enabled = it)) }), verticalAlignment = Alignment.CenterVertically) {
            Column(Modifier.weight(1f)) {
                Text("Game sounds", color = palette.text)
                Text("Pass, start, pause, resume and game over", color = palette.muted,
                    style = MaterialTheme.typography.bodySmall)
            }
            Switch(prefs.enabled, onCheckedChange = null)
        }
        Text("Sound volume · ${prefs.volume}%", color = palette.muted, style = MaterialTheme.typography.labelLarge)
        Slider(prefs.volume.toFloat(), { audio.onChange(prefs.copy(volume = it.toInt().coerceIn(0, 100))) },
            enabled = prefs.enabled, valueRange = 0f..100f, steps = 3,
            modifier = Modifier.semantics { contentDescription = "Game sound volume" })
    }
}
