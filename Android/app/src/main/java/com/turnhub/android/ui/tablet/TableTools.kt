package com.turnhub.android.ui.tablet

import androidx.compose.animation.core.Animatable
import androidx.compose.animation.core.tween
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
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
import com.turnhub.android.domain.TableSummary
import com.turnhub.android.domain.TablePlayer
import kotlin.random.Random

/**
 * Table tools: dice, a coin and a random player. They only help the table
 * decide things the game itself leaves to the players, so they never change
 * game state and work the same with or without Atlas.
 */
internal object TableTools {
    val dice = (4..20).toList()
    val commonDice = listOf(4, 6, 8, 10, 12, 20)
    fun roll(sides: Int, random: Random = Random.Default): String {
        require(sides in dice)
        return "d$sides: ${random.nextInt(1, sides + 1)}"
    }
    fun flip(random: Random = Random.Default): String = if (random.nextBoolean()) "Heads" else "Tails"
    fun pick(names: List<String>, random: Random = Random.Default): String? =
        names.takeIf { it.isNotEmpty() }?.let { it[random.nextInt(it.size)] }
    fun opponents(summary: TableSummary, playerNumber: Int): List<TablePlayer> {
        if (summary.players.none { it.playerNumber == playerNumber && !it.eliminated }) return emptyList()
        return summary.players.filter { !it.eliminated && !summary.sameTeam(playerNumber, it.playerNumber) &&
            (!summary.star || it in summary.starEnemies(playerNumber)) }
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
internal fun TableToolsCard(summary: TableSummary, reduceMotion: Boolean, onUse: () -> Unit) {
    val p = palette
    val players = summary.players.filter { !it.eliminated }
    var choosingDie by remember { mutableStateOf(false) }
    fun label(player: TablePlayer) = "${player.label} · Seat ${player.playerNumber}"
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
        Text("Dice & coin", color = p.muted, style = MaterialTheme.typography.labelLarge)
        FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
            TableTools.commonDice.forEach { sides -> ToneButton("d$sides", { show(TableTools.roll(sides)) }) }
            Box {
                ToneButton("More dice ▾", { choosingDie = true })
                DropdownMenu(choosingDie, { choosingDie = false }) {
                    TableTools.dice.filterNot { it in TableTools.commonDice }.forEach { sides ->
                        DropdownMenuItem({ Text("d$sides") }, { choosingDie = false; show(TableTools.roll(sides)) })
                    }
                }
            }
            ToneButton("Coin", { show(TableTools.flip()) })
        }
        ToneButton("Pick a random player", { show(TableTools.pick(players.map(::label))) }, Modifier.fillMaxWidth(), enabled = players.isNotEmpty())
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

/** A player-scoped tool: the open More card supplies the seat, never a name lookup. */
@Composable
internal fun OpponentPicker(summary: TableSummary, playerNumber: Int) {
    val opponents = TableTools.opponents(summary, playerNumber)
    var picked by remember(playerNumber, opponents.map { it.playerNumber }) { mutableStateOf<String?>(null) }
    val haptics = com.turnhub.android.ui.components.rememberHaptics()
    ToneButton("Pick an opponent", {
        haptics.tick()
        picked = TableTools.pick(opponents.map { "${it.label} · Seat ${it.playerNumber}" })
    }, Modifier.fillMaxWidth(), enabled = opponents.isNotEmpty())
    picked?.let { Text(it, color = palette.text, style = MaterialTheme.typography.titleLarge,
        modifier = Modifier.fillMaxWidth().semantics { liveRegion = LiveRegionMode.Polite }, textAlign = TextAlign.Center) }
}
