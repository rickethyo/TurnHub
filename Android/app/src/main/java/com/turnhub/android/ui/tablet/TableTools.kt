package com.turnhub.android.ui.tablet

import androidx.compose.animation.core.Animatable
import androidx.compose.animation.core.tween
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.semantics.LiveRegionMode
import androidx.compose.ui.semantics.liveRegion
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import com.turnhub.android.ui.components.ToneButton
import com.turnhub.android.ui.theme.palette
import kotlin.random.Random

/**
 * Table tools: dice, a coin and a random player. They only help the table
 * decide things the game itself leaves to the players, so they never change
 * game state and work the same with or without Atlas.
 */
internal object TableTools {
    fun roll(sides: Int, random: Random = Random.Default): String = "d$sides: ${random.nextInt(1, sides + 1)}"
    fun flip(random: Random = Random.Default): String = if (random.nextBoolean()) "Heads" else "Tails"
    fun pick(names: List<String>, random: Random = Random.Default): String? =
        names.takeIf { it.isNotEmpty() }?.let { it[random.nextInt(it.size)] }
}

@Composable
internal fun TableToolsCard(players: List<String>, reduceMotion: Boolean, onUse: () -> Unit) {
    val p = palette
    var result by remember { mutableStateOf<String?>(null) }
    var uses by remember { mutableIntStateOf(0) }
    fun show(value: String?) {
        if (value == null) return
        onUse()
        result = value
        uses++
    }
    // A short tumble so a repeated result still reads as a new one.
    val spin = remember { Animatable(0f) }
    LaunchedEffect(uses) {
        if (uses == 0 || reduceMotion) return@LaunchedEffect
        spin.snapTo(-90f)
        spin.animateTo(0f, tween(260))
    }
    Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
        Text("Table tools", color = p.muted, style = MaterialTheme.typography.labelLarge)
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            ToneButton("d6", { show(TableTools.roll(6)) }, Modifier.weight(1f))
            ToneButton("d20", { show(TableTools.roll(20)) }, Modifier.weight(1f))
            ToneButton("Coin", { show(TableTools.flip()) }, Modifier.weight(1f))
        }
        ToneButton("Pick a random player", { show(TableTools.pick(players)) }, Modifier.fillMaxWidth(), enabled = players.isNotEmpty())
        result?.let {
            Text(
                it,
                color = p.text,
                style = MaterialTheme.typography.headlineMedium,
                fontWeight = FontWeight.Bold,
                textAlign = TextAlign.Center,
                modifier = Modifier
                    .fillMaxWidth()
                    .graphicsLayer { rotationX = spin.value }
                    .semantics { liveRegion = LiveRegionMode.Polite },
            )
        }
    }
}
