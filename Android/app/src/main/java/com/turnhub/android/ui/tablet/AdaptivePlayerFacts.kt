package com.turnhub.android.ui.tablet

import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.rememberTextMeasurer
import androidx.compose.ui.unit.Constraints
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.TextUnit
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.turnhub.android.domain.TableSummary
import com.turnhub.android.ui.components.CounterChip

internal data class CommanderDamageFact(val source: Int, val name: String, val total: Int) {
    val text: String get() = "⚔ $name $total"
}

internal fun commanderDamageFacts(unit: TableUnit, summary: TableSummary): List<CommanderDamageFact> {
    if (!summary.settings.profile.commanderRules) return emptyList()
    return unit.members.flatMap { member ->
        summary.players.filter { it.playerNumber != member.playerNumber }.map { source ->
            val damage = member.commanderDamage.firstOrNull { it.sourcePlayer == source.playerNumber }?.damage.orEmpty()
            CommanderDamageFact(source.playerNumber, source.label,
                (1..2).sumOf { c -> damage.getOrNull(c - 1) ?: 0 })
        }
    }.filter { it.total > 0 }
}

/** Grow facts only while their wrapped rows fit the allocated band, leaving the art and score visible. */
@Composable
internal fun adaptiveFactTextSize(counterRows: List<List<CounterChip>>, damage: List<CommanderDamageFact>,
    width: Dp, heightBudget: Dp, target: Int): TextUnit {
    val measure = rememberTextMeasurer()
    val density = LocalDensity.current
    val base = MaterialTheme.typography.labelMedium
    val widthPx = with(density) { width.roundToPx() }.coerceAtLeast(1)
    val budgetPx = with(density) { heightBudget.roundToPx() }
    val gap = with(density) { 6.dp.roundToPx() }
    val counterPad = with(density) { 14.dp.roundToPx() }
    val damagePad = with(density) { 16.dp.roundToPx() }
    val verticalPad = with(density) { 6.dp.roundToPx() }
    fun packedHeight(items: List<Pair<Int, Int>>, rowGap: Int): Int {
        var used = 0; var row = 0; var total = 0
        items.forEach { (width, height) ->
            if (used > 0 && used + gap + width > widthPx) { total += row + rowGap; used = 0; row = 0 }
            used += (if (used > 0) gap else 0) + width
            row = maxOf(row, height)
        }
        return total + row
    }
    val chosen = (target.coerceIn(12, 24) downTo 12).firstOrNull { font ->
        val style = base.copy(fontSize = font.sp, lineHeight = (font * 1.2f).sp)
        val damageItems = damage.map {
            val text = measure.measure(it.text, style.copy(fontWeight = if (it.total >= 21) FontWeight.Bold else FontWeight.Medium),
                constraints = Constraints(maxWidth = (widthPx - damagePad).coerceAtLeast(1)))
            (text.size.width + damagePad) to (text.size.height + with(density) { 4.dp.roundToPx() })
        }
        var countersFit = true
        val countersHeight = counterRows.filter { it.isNotEmpty() }.sumOf { row ->
            val items = row.map { chip ->
                val pieces = buildList {
                    if (chip.icon != null) add(with(density) { font.sp.toDp().roundToPx() } * 1.2f to 0)
                    else measure.measure(chip.glyph, style).size.let { add(it.width.toFloat() to it.height) }
                    chip.value?.let { measure.measure(it, style.copy(fontWeight = FontWeight.Bold)).size.let { size -> add(size.width.toFloat() to size.height) } }
                    if (chip.value == null) measure.measure(chip.label, style).size.let { add(it.width.toFloat() to it.height) }
                }
                val pieceGap = with(density) { 5.dp.roundToPx() }
                (pieces.sumOf { it.first.toDouble() }.toInt() + (pieces.size - 1) * pieceGap + counterPad) to
                    (pieces.maxOfOrNull { it.second }?.coerceAtLeast(with(density) { font.sp.toPx().toInt() }) ?: 0) + verticalPad
            }
            if (items.any { it.first > widthPx }) countersFit = false
            packedHeight(items, gap) + with(density) { 4.dp.roundToPx() }
        }
        countersFit && packedHeight(damageItems, with(density) { 4.dp.roundToPx() }) + countersHeight <= budgetPx
    } ?: 12
    return chosen.sp
}
