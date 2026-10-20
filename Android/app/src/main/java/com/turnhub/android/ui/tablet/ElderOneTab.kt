package com.turnhub.android.ui.tablet

import androidx.compose.animation.animateColorAsState
import androidx.compose.animation.core.LinearEasing
import androidx.compose.animation.core.RepeatMode
import androidx.compose.animation.core.animateFloat
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.infiniteRepeatable
import androidx.compose.animation.core.rememberInfiniteTransition
import androidx.compose.animation.core.snap
import androidx.compose.animation.core.spring
import androidx.compose.animation.core.tween
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.drawBehind
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.PathEffect
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.drawscope.rotate
import androidx.compose.ui.semantics.LiveRegionMode
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.clearAndSetSemantics
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.liveRegion
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.turnhub.android.domain.DeathMayDieStory
import com.turnhub.android.domain.ElderOneTrack
import com.turnhub.android.domain.StoryGroup
import com.turnhub.android.protocol.TableState
import com.turnhub.android.standalone.StandaloneState
import com.turnhub.android.standalone.StandaloneTable
import com.turnhub.android.ui.components.AccentButton
import com.turnhub.android.ui.components.BrassCard
import com.turnhub.android.ui.components.DisclosureCard
import com.turnhub.android.ui.components.Eyebrow
import com.turnhub.android.ui.components.StatusBadge
import com.turnhub.android.ui.components.Tone
import com.turnhub.android.ui.components.ToneButton
import com.turnhub.android.ui.theme.TABULAR
import com.turnhub.android.ui.theme.palette

/**
 * The Elder One tab (Death May Die on this device): which Elder One and episode the table
 * plays, then the Story board during the game: Summoning symbols, the Summoning track, the
 * ritual and the Elder One's stages ([ElderOneTrack]). Card effects stay on the physical
 * cards; every control here is a reminder the table can correct.
 */
@Composable
fun ElderOneTab(state: StandaloneState, table: StandaloneTable, reduceMotion: Boolean = false) {
    val game = state.game
    val elder = game.elder
    val playing = game.state == TableState.RUNNING || game.state == TableState.PAUSED
    val running = game.state == TableState.RUNNING
    val over = game.state == TableState.GAME_OVER
    var picking by rememberSaveable { mutableStateOf<String?>(null) }
    Column(
        Modifier.widthIn(max = 720.dp).fillMaxWidth().verticalScroll(rememberScrollState()).padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(14.dp),
    ) {
        ElderHero(
            elder = elder,
            mood = when {
                over && game.winners.isNotEmpty() -> HeroMood.WON
                over -> HeroMood.LOST
                elder.summoned -> HeroMood.SUMMONED
                else -> HeroMood.WAITING
            },
            enabled = !over,
            reduceMotion = reduceMotion,
            onPickElder = { picking = "elder" },
            onPickEpisode = { picking = "episode" },
        )
        when {
            playing -> {
                SummoningCard(elder, running, reduceMotion, table)
                if (elder.summoned) StageCard(elder, running, reduceMotion, table)
                else RitualCard(running, table)
                EpisodeTokensCard(elder, counting = running, table)
                BrassCard { DeathMayDieTurnPhasesButton() }
                StoryRules()
            }
            over -> Unit
            else -> {
                BrassCard {
                    Eyebrow("The Story board")
                    Text("Once the game starts, this tab follows the Summoning track, the ritual and the Elder One's stages.",
                        color = palette.text)
                    Text("Choosing an Elder One also means an investigator lost before it is summoned ends the game, as the rules say.",
                        color = palette.muted, style = MaterialTheme.typography.bodyMedium)
                }
                EpisodeTokensCard(elder, counting = false, table)
                StoryRules()
            }
        }
    }
    when (picking) {
        "elder" -> StoryPickerDialog("Elder One", DeathMayDieStory.elderOnes, elder.elderOne,
            { table.setElderOne(it); picking = null }) { picking = null }
        "episode" -> StoryPickerDialog("Episode", DeathMayDieStory.episodes, elder.episode,
            { table.setEpisode(it); picking = null }) { picking = null }
    }
}

/** The same visual board for a guest display; only the host can move its markers. */
@Composable
internal fun ElderOneDisplay(game: com.turnhub.android.standalone.StandaloneGame, reduceMotion: Boolean) {
    val elder = game.elder
    ElderHero(elder, when {
        game.state == TableState.GAME_OVER && game.winners.isNotEmpty() -> HeroMood.WON
        game.state == TableState.GAME_OVER -> HeroMood.LOST
        elder.summoned -> HeroMood.SUMMONED
        else -> HeroMood.WAITING
    }, enabled = false, reduceMotion = reduceMotion, onPickElder = {}, onPickEpisode = {})
    BrassCard {
        Eyebrow("Summoning track · ${elder.space}/${ElderOneTrack.LAST_SPACE}")
        TrackPath(elder.space, reduceMotion)
        Text("Summoning symbols: ${elder.symbols}/${ElderOneTrack.SYMBOLS_TO_ADVANCE}", color = palette.text)
        Text(if (elder.ritualDisrupted) "Ritual disrupted" else "Ritual intact", color = palette.text)
    }
    if (elder.summoned) BrassCard {
        StageLadder(elder.stage)
        WoundRing(elder, reduceMotion)
        Text("${ElderOneTrack.stageName(elder.stage)} · ${elder.stageWounds}/${elder.health ?: "?"} wounds", color = palette.text)
    }
    if (elder.tokens.isNotEmpty()) BrassCard {
        Eyebrow("Episode tokens")
        elder.tokens.forEach { Text("${it.name}: ${it.count}", color = palette.text, style = MaterialTheme.typography.titleLarge) }
    }
}

/**
 * The episode's own tokens (gates, seals and so on): the table names them, in setup or
 * during the game, and counts them while it plays. Each count is a number, never only a picture.
 */
@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun EpisodeTokensCard(elder: ElderOneTrack, counting: Boolean, table: StandaloneTable) {
    val p = palette
    var naming by rememberSaveable { mutableStateOf(false) }
    BrassCard {
        Eyebrow("Episode tokens")
        if (elder.tokens.isEmpty()) Text("Add a counter for each token your episode uses, like gates or seals.", color = p.muted,
            style = MaterialTheme.typography.bodyMedium)
        elder.tokens.forEachIndexed { index, token ->
            Row(Modifier.fillMaxWidth().clip(RoundedCornerShape(16.dp)).background(p.inset).padding(horizontal = 14.dp, vertical = 8.dp),
                verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                Text(token.name, color = p.text, style = MaterialTheme.typography.titleMedium, fontWeight = FontWeight.Bold,
                    modifier = Modifier.weight(1f))
                StepButton("−", "Remove a ${token.name} token", counting && token.count > 0) { table.changeEpisodeToken(index, -1) }
                Text("${token.count}", color = p.text, style = MaterialTheme.typography.headlineSmall, fontWeight = FontWeight.Black,
                    textAlign = TextAlign.Center,
                    modifier = Modifier.widthIn(min = 40.dp).semantics { contentDescription = "${token.count} ${token.name}"; liveRegion = LiveRegionMode.Polite })
                StepButton("+", "Add a ${token.name} token", counting && token.count < ElderOneTrack.MAX_TOKEN_COUNT) { table.changeEpisodeToken(index, 1) }
                if (token.count == 0) TextButton({ table.removeEpisodeToken(index) }) { Text("Remove") }
            }
        }
        if (elder.tokens.size < ElderOneTrack.MAX_TOKENS) {
            val unused = ElderOneTrack.SUGGESTED_TOKENS.filter { name -> elder.tokens.none { it.name.equals(name, ignoreCase = true) } }
            FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                unused.forEach { name -> ToneButton("+ $name", { table.nameEpisodeToken(name) }) }
                ToneButton("+ Other…", { naming = true })
            }
        }
    }
    if (naming) {
        var name by rememberSaveable { mutableStateOf("") }
        AlertDialog(
            onDismissRequest = { naming = false },
            title = { Text("Name the token") },
            text = {
                OutlinedTextField(name, { name = it.take(ElderOneTrack.MAX_NAME) }, singleLine = true, label = { Text("Token") })
            },
            confirmButton = { TextButton({ table.nameEpisodeToken(name); naming = false }, enabled = name.isNotBlank()) { Text("Add") } },
            dismissButton = { TextButton({ naming = false }) { Text("Cancel") } },
        )
    }
}

private enum class HeroMood { WAITING, SUMMONED, WON, LOST }

/**
 * The Elder One's name over a slowly turning summoning circle, its glow green while it waits
 * on the track, red once it is on the board, gold when it falls. The words carry the state;
 * the circle and color only decorate it.
 */
@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun ElderHero(
    elder: ElderOneTrack,
    mood: HeroMood,
    enabled: Boolean,
    reduceMotion: Boolean,
    onPickElder: () -> Unit,
    onPickEpisode: () -> Unit,
) {
    val p = palette
    val target = when (mood) {
        HeroMood.WAITING -> p.good
        HeroMood.SUMMONED, HeroMood.LOST -> p.bad
        HeroMood.WON -> p.accent
    }
    val glow by animateColorAsState(target, if (reduceMotion) snap() else tween(700), label = "heroGlow")
    val transition = rememberInfiniteTransition(label = "hero")
    val turn by transition.animateFloat(0f, 360f, infiniteRepeatable(tween(60_000, easing = LinearEasing)), label = "turn")
    val breath by transition.animateFloat(.55f, 1f, infiniteRepeatable(tween(2_400), RepeatMode.Reverse), label = "breath")
    val angle = if (reduceMotion) 0f else turn
    val pulse = if (reduceMotion || mood == HeroMood.WAITING) .8f else breath
    val shape = RoundedCornerShape(24.dp)
    Box(
        Modifier.fillMaxWidth().heightIn(min = 196.dp).clip(shape)
            .background(Brush.verticalGradient(listOf(p.surface3, p.inset)))
            .border(1.dp, glow.copy(alpha = .55f), shape),
    ) {
        Canvas(Modifier.matchParentSize().clearAndSetSemantics { }) {
            val c = Offset(size.width * .82f, size.height * .42f)
            val r = size.height * .62f
            drawCircle(Brush.radialGradient(listOf(glow.copy(alpha = .42f * pulse), Color.Transparent), c, r * 1.25f), r * 1.25f, c)
            rotate(angle, c) {
                drawCircle(glow.copy(alpha = .5f), r, c, style = Stroke(1.5.dp.toPx()))
                drawCircle(glow.copy(alpha = .35f), r * .78f, c,
                    style = Stroke(2.dp.toPx(), pathEffect = PathEffect.dashPathEffect(floatArrayOf(14f, 10f))))
                // A seven-pointed star joining every third of seven points.
                val points = (0 until 7).map { i ->
                    val a = Math.toRadians(i * 360.0 / 7 - 90).toFloat()
                    Offset(c.x + r * .78f * kotlin.math.cos(a), c.y + r * .78f * kotlin.math.sin(a))
                }
                points.indices.forEach { i ->
                    drawLine(glow.copy(alpha = .3f), points[i], points[(i + 3) % 7], 1.5.dp.toPx(), StrokeCap.Round)
                }
            }
            rotate(-angle * 1.5f, c) {
                drawCircle(glow.copy(alpha = .4f), r * .42f, c,
                    style = Stroke(1.dp.toPx(), pathEffect = PathEffect.dashPathEffect(floatArrayOf(4f, 8f))))
            }
            drawCircle(Brush.radialGradient(listOf(glow.copy(alpha = .9f * pulse), glow.copy(alpha = 0f)), c, r * .2f), r * .2f, c)
            // A sheen across the top, the app's glossy finish.
            drawRect(Brush.verticalGradient(0f to Color.White.copy(alpha = if (p.dark) .06f else .25f), .45f to Color.Transparent))
        }
        Column(Modifier.padding(20.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
            Text("THE ELDER ONE", color = glow, style = MaterialTheme.typography.labelMedium.copy(letterSpacing = 2.5.sp),
                fontWeight = FontWeight.Bold)
            Text(
                elder.elderOne ?: "Not chosen yet",
                color = if (elder.elderOne == null) p.muted else p.text,
                style = MaterialTheme.typography.headlineLarge,
                fontWeight = FontWeight.Black,
                modifier = Modifier.semantics { heading() },
            )
            Text(elder.episode?.let { "Episode · $it" } ?: "No episode chosen", color = p.muted,
                style = MaterialTheme.typography.titleSmall)
            FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalArrangement = Arrangement.spacedBy(6.dp),
                modifier = Modifier.padding(top = 4.dp)) {
                when (mood) {
                    HeroMood.WAITING -> StatusBadge("On the Summoning track", Tone.GOOD)
                    HeroMood.SUMMONED -> {
                        StatusBadge("Summoned · ${ElderOneTrack.stageName(elder.stage)}", Tone.BAD)
                        StatusBadge(if (elder.ritualDisrupted) "Can be wounded" else "Can't be wounded yet",
                            if (elder.ritualDisrupted) Tone.WARN else Tone.NEUTRAL)
                    }
                    HeroMood.WON -> StatusBadge("Banished: the investigators won", Tone.GOOD)
                    HeroMood.LOST -> StatusBadge("The investigators lost", Tone.BAD)
                }
            }
            if (enabled) Row(horizontalArrangement = Arrangement.spacedBy(8.dp), modifier = Modifier.padding(top = 6.dp)) {
                ToneButton(if (elder.elderOne == null) "Choose Elder One" else "Change Elder One", onPickElder)
                ToneButton(if (elder.episode == null) "Choose episode" else "Change episode", onPickEpisode)
            }
        }
    }
}

/** The Summoning track's 8 spaces and the Mythos discard's Summoning symbols. */
@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun SummoningCard(elder: ElderOneTrack, running: Boolean, reduceMotion: Boolean, table: StandaloneTable) {
    val p = palette
    val marker = if (elder.summoned) "Progression token" else "Elder One"
    val zone = when {
        elder.space >= ElderOneTrack.LAST_SPACE -> "the last space"
        elder.space >= ElderOneTrack.FIRST_RED_SPACE -> "red"
        else -> "green"
    }
    BrassCard {
        Eyebrow("Summoning track") {
            Text("${elder.space}/${ElderOneTrack.LAST_SPACE}", color = p.text, fontWeight = FontWeight.Black,
                style = MaterialTheme.typography.titleMedium.copy(fontFeatureSettings = TABULAR))
        }
        Text("$marker on space ${elder.space}, $zone", color = p.text, style = MaterialTheme.typography.titleMedium,
            fontWeight = FontWeight.SemiBold, modifier = Modifier.semantics { liveRegion = LiveRegionMode.Polite })
        TrackPath(elder.space, reduceMotion)
        FlowRow(horizontalArrangement = Arrangement.spacedBy(10.dp), verticalArrangement = Arrangement.spacedBy(8.dp),
            itemVerticalAlignment = Alignment.CenterVertically) {
            StepButton("−", "Move the $marker back a space", running && elder.space > ElderOneTrack.START_SPACE) {
                table.changeElder { it.advance(-1) }
            }
            if (elder.space + 1 < ElderOneTrack.LAST_SPACE) {
                ToneButton("Advance the $marker", { table.changeElder { it.advance(1) } }, enabled = running)
            }
        }
        if (running && elder.space + 1 >= ElderOneTrack.LAST_SPACE) {
            ArmedButton("Advance to the last space", "Tap again: the investigators lose", Tone.BAD) { table.changeElder { it.advance(1) } }
        }
        SymbolsRow(elder, running, table)
    }
}

/**
 * Eight sigils on a path, green then red then the last space; a glowing marker slides to the
 * current one. Purely decorative: the line above names the space and its color.
 */
@Composable
private fun TrackPath(space: Int, reduceMotion: Boolean) {
    val p = palette
    val last = ElderOneTrack.LAST_SPACE
    val at by animateFloatAsState(space.toFloat(), if (reduceMotion) snap() else spring(dampingRatio = .62f, stiffness = 120f), label = "track")
    val breath by rememberInfiniteTransition(label = "marker")
        .animateFloat(.6f, 1f, infiniteRepeatable(tween(1_400), RepeatMode.Reverse), label = "markerGlow")
    val glow = if (reduceMotion) .85f else breath
    fun tint(n: Int) = when {
        n == last -> p.text
        n >= ElderOneTrack.FIRST_RED_SPACE -> p.bad
        else -> p.good
    }
    Box(Modifier.fillMaxWidth().height(64.dp).clearAndSetSemantics { }) {
        Canvas(Modifier.fillMaxSize()) {
            val step = size.width / last
            val y = size.height / 2
            fun x(n: Float) = step * (n - .5f)
            val line = 6.dp.toPx()
            drawLine(p.good.copy(alpha = .45f), Offset(x(1f), y), Offset(x(ElderOneTrack.FIRST_RED_SPACE - .5f), y), line, StrokeCap.Round)
            drawLine(p.bad.copy(alpha = .45f), Offset(x(ElderOneTrack.FIRST_RED_SPACE - .5f), y), Offset(x(last - .5f), y), line, StrokeCap.Round)
            drawLine(p.text.copy(alpha = .35f), Offset(x(last - .5f), y), Offset(x(last.toFloat()), y), line, StrokeCap.Round)
            // The path already walked glows brighter.
            drawLine(tint(space).copy(alpha = .9f), Offset(x(1f), y), Offset(x(at), y), line * .5f, StrokeCap.Round)
            val markerColor = tint(space)
            val mc = Offset(x(at), y)
            drawCircle(Brush.radialGradient(listOf(markerColor.copy(alpha = .55f * glow), Color.Transparent), mc, step * .75f), step * .75f, mc)
        }
        Row(Modifier.fillMaxSize(), verticalAlignment = Alignment.CenterVertically) {
            (ElderOneTrack.START_SPACE..last).forEach { n ->
                val here = n == space
                val passed = n < space
                val color = tint(n)
                Box(Modifier.weight(1f), contentAlignment = Alignment.Center) {
                    val dot = if (here) 44.dp else 30.dp
                    Box(
                        Modifier.size(dot).clip(CircleShape)
                            .background(
                                if (here) Brush.radialGradient(listOf(color, color.copy(alpha = .75f)))
                                else Brush.verticalGradient(listOf(p.surface3, p.surface2)),
                            )
                            .border(if (here) 3.dp else 1.5.dp, if (here) p.text else color.copy(alpha = if (passed) .95f else .55f), CircleShape),
                        contentAlignment = Alignment.Center,
                    ) {
                        Text(if (n == last) "✕" else "$n", color = if (here) p.surface else if (passed) color else p.text,
                            fontWeight = if (here) FontWeight.Black else FontWeight.SemiBold,
                            style = if (here) MaterialTheme.typography.titleMedium else MaterialTheme.typography.labelLarge)
                    }
                }
            }
        }
    }
}

/** Three Summoning symbols, drawn as sigils that fill in; the third advances the track. */
@Composable
private fun SymbolsRow(elder: ElderOneTrack, running: Boolean, table: StandaloneTable) {
    val p = palette
    val ends = elder.symbols + 1 >= ElderOneTrack.SYMBOLS_TO_ADVANCE && elder.space + 1 >= ElderOneTrack.LAST_SPACE
    Column(
        Modifier.fillMaxWidth().clip(RoundedCornerShape(16.dp)).background(p.inset).padding(14.dp),
        verticalArrangement = Arrangement.spacedBy(10.dp),
    ) {
        Text("Summoning symbols in the Mythos discards", color = p.muted, style = MaterialTheme.typography.labelLarge)
        Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(10.dp)) {
            StepButton("−", "Remove a Summoning symbol", running && elder.symbols > 0) { table.changeElder { it.symbol(-1) } }
            Row(Modifier.weight(1f).semantics(mergeDescendants = true) {
                contentDescription = "${elder.symbols} of ${ElderOneTrack.SYMBOLS_TO_ADVANCE} Summoning symbols"
                liveRegion = LiveRegionMode.Polite
            }, horizontalArrangement = Arrangement.Center) {
                repeat(ElderOneTrack.SYMBOLS_TO_ADVANCE) { i -> SymbolSigil(i < elder.symbols) }
            }
            if (!ends) StepButton("+", "Add a Summoning symbol", running) { table.changeElder { it.symbol(1) } }
        }
        if (running && ends) {
            ArmedButton("Third Summoning symbol", "Tap again: the investigators lose", Tone.BAD) { table.changeElder { it.symbol(1) } }
        }
    }
}

@Composable
private fun SymbolSigil(lit: Boolean) {
    val p = palette
    Box(Modifier.padding(horizontal = 6.dp).size(40.dp).clearAndSetSemantics { }, contentAlignment = Alignment.Center) {
        Canvas(Modifier.fillMaxSize()) {
            val c = center
            val r = size.minDimension / 2
            if (lit) drawCircle(Brush.radialGradient(listOf(p.bad.copy(alpha = .6f), Color.Transparent), c, r), r, c)
            drawCircle(if (lit) p.bad else p.line, r * .7f, c, style = Stroke(2.dp.toPx()))
            val points = (0 until 5).map { i ->
                val a = Math.toRadians(i * 72.0 - 90).toFloat()
                Offset(c.x + r * .55f * kotlin.math.cos(a), c.y + r * .55f * kotlin.math.sin(a))
            }
            points.indices.forEach { i ->
                drawLine(if (lit) p.text else p.faint, points[i], points[(i + 2) % 5], 2.dp.toPx(), StrokeCap.Round)
            }
        }
    }
}

/** Before it is summoned: disrupting the ritual summons it, or it can arrive another way. */
@Composable
private fun RitualCard(running: Boolean, table: StandaloneTable) {
    val p = palette
    BrassCard(highlight = p.good.copy(alpha = .6f)) {
        Eyebrow("The ritual")
        Text("Stage 1 is on top: the Elder One waits on the Summoning track.", color = p.text, fontWeight = FontWeight.SemiBold)
        Text("Until it is summoned, an investigator who dies or goes insane loses the game. Do your episode's task to " +
            "disrupt the ritual: that summons the Elder One at the end of the turn and makes it mortal.", color = p.muted)
        AccentButton("We disrupted the ritual", { table.changeElder { it.disrupt() } }, Modifier.fillMaxWidth(), enabled = running)
        TextButton({ table.changeElder { it.summon() } }, enabled = running) { Text("It was summoned another way") }
    }
}

/** Once summoned: the stage ladder, a ring of wounds against the stage's health, and how it falls. */
@Composable
private fun StageCard(elder: ElderOneTrack, running: Boolean, reduceMotion: Boolean, table: StandaloneTable) {
    val p = palette
    val stage = ElderOneTrack.stageName(elder.stage)
    BrassCard(highlight = if (elder.vulnerable) p.bad else null) {
        Eyebrow("Stages")
        StageLadder(elder.stage)
        if (!elder.ritualDisrupted) {
            Text("It can't be attacked or wounded until the ritual is disrupted.", color = p.text, fontWeight = FontWeight.SemiBold)
            AccentButton("We disrupted the ritual", { table.changeElder { it.disrupt() } }, Modifier.fillMaxWidth(), enabled = running)
            if (elder.stage == 2) TextButton({ table.changeElder { it.unsummon() } }, enabled = running) { Text("Undo: it isn't summoned yet") }
            return@BrassCard
        }
        Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.SpaceEvenly) {
            StepButton("−", "Remove a wound from the Elder One", running && elder.stageWounds > 0) { table.changeElder { it.wound(-1) } }
            WoundRing(elder, reduceMotion)
            StepButton("+", "Add a wound to the Elder One", running && elder.stageWounds < ElderOneTrack.MAX_WOUNDS) { table.changeElder { it.wound(1) } }
        }
        HealthField(elder, running, table)
        if (elder.stageBeaten) Text("$stage has taken its health in wounds: it is defeated.", color = p.text, fontWeight = FontWeight.SemiBold,
            textAlign = TextAlign.Center, modifier = Modifier.fillMaxWidth())
        if (elder.stage < ElderOneTrack.FINAL_STAGE) {
            val next = ElderOneTrack.stageName(elder.stage + 1)
            val defeat = { table.changeElder { it.defeatStage() } }
            if (elder.stageBeaten) AccentButton("Reveal $next", defeat, Modifier.fillMaxWidth(), enabled = running)
            else ToneButton("$stage defeated: reveal $next", defeat, Modifier.fillMaxWidth(), enabled = running)
            Text("Extra wounds don't carry over. Resolve $next's When Revealed effect.", color = p.faint, style = MaterialTheme.typography.bodySmall)
        } else if (running) {
            ArmedButton("We killed the Elder One", "Tap again: the investigators win", Tone.GOOD) { table.killElderOne() }
        }
        if (elder.stageWounds == 0) TextButton({ table.changeElder { it.undisrupt() } }, enabled = running) { Text("Undo: the ritual isn't disrupted") }
    }
}

/** Stage 1 → Stage 2 → Stage 3 → Final, with the beaten ones ticked and the current one lit. */
@Composable
private fun StageLadder(current: Int) {
    val p = palette
    Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
        (1..ElderOneTrack.FINAL_STAGE).forEach { n ->
            val here = n == current
            val done = n < current
            val pill = RoundedCornerShape(12.dp)
            val name = if (n == ElderOneTrack.FINAL_STAGE) "Final" else "Stage $n"
            Box(
                Modifier.weight(1f).heightIn(min = 40.dp).clip(pill)
                    .background(if (here) Brush.verticalGradient(listOf(p.bad, p.bad.copy(alpha = .7f))) else Brush.verticalGradient(listOf(p.surface2, p.surface2)))
                    .border(1.dp, if (here) p.text.copy(alpha = .6f) else p.line, pill)
                    .semantics(mergeDescendants = true) {
                        selected = here
                        contentDescription = name + when { here -> ", current"; done -> ", defeated"; else -> "" }
                    },
                contentAlignment = Alignment.Center,
            ) {
                Text(if (done) "✓ $name" else name, color = if (here) Color.White else if (done) p.muted else p.text,
                    fontWeight = if (here) FontWeight.Black else FontWeight.SemiBold, style = MaterialTheme.typography.labelLarge, maxLines = 1)
            }
        }
    }
}

/** Wounds against the printed health, as a filling ring; without a health typed, just the count. */
@Composable
private fun WoundRing(elder: ElderOneTrack, reduceMotion: Boolean) {
    val p = palette
    val health = elder.health
    val fraction = if (health == null) 0f else (elder.stageWounds.toFloat() / health).coerceIn(0f, 1f)
    val shown by animateFloatAsState(fraction, if (reduceMotion) snap() else tween(450), label = "wounds")
    Box(
        Modifier.size(150.dp).semantics(mergeDescendants = true) {
            contentDescription = "${elder.stageWounds} wounds" + (health?.let { " of $it health" } ?: "")
            liveRegion = LiveRegionMode.Polite
        },
        contentAlignment = Alignment.Center,
    ) {
        Canvas(Modifier.fillMaxSize()) {
            val stroke = 12.dp.toPx()
            val inset = stroke / 2
            val arc = Size(size.width - stroke, size.height - stroke)
            val tl = Offset(inset, inset)
            drawArc(p.fillStrong, 135f, 270f, false, tl, arc, style = Stroke(stroke, cap = StrokeCap.Round))
            if (shown > 0f) {
                drawArc(Brush.sweepGradient(listOf(p.warn, p.bad, p.warn)), 135f, 270f * shown, false, tl, arc,
                    style = Stroke(stroke, cap = StrokeCap.Round))
            }
            if (elder.stageBeaten) drawCircle(Brush.radialGradient(listOf(p.bad.copy(alpha = .35f), Color.Transparent)), size.minDimension / 2.4f)
        }
        Column(horizontalAlignment = Alignment.CenterHorizontally) {
            Text("${elder.stageWounds}", color = p.text, fontWeight = FontWeight.Black,
                style = MaterialTheme.typography.displaySmall.copy(fontFeatureSettings = TABULAR))
            Text(health?.let { "of $it health" } ?: "wounds", color = p.muted, style = MaterialTheme.typography.labelMedium)
        }
    }
}

/** The health printed on the current stage, typed once as the stage is revealed. */
@Composable
private fun HealthField(elder: ElderOneTrack, running: Boolean, table: StandaloneTable) {
    var text by rememberSaveable(elder.stage) { mutableStateOf(elder.health?.toString().orEmpty()) }
    LaunchedEffect(elder.health) { if (text.toIntOrNull() != elder.health) text = elder.health?.toString().orEmpty() }
    OutlinedTextField(text, { value ->
        text = value.filter(Char::isDigit).take(2)
        table.changeElder { it.withHealth(text.toIntOrNull()?.takeIf { n -> n > 0 }) }
    }, label = { Text("Health printed on ${ElderOneTrack.stageName(elder.stage)}") }, singleLine = true, enabled = running,
        keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number), modifier = Modifier.fillMaxWidth())
}

/** The Story board rules in brief, folded away until someone asks. */
@Composable
private fun StoryRules() {
    val p = palette
    DisclosureCard("How the Story board works") {
        listOf(
            "Spaces 1 to 4 are green, 5 to 7 red. Entering the first red space summons the Elder One; the Progression token reaching space 8 loses the game.",
            "Count the discarded Mythos cards that show a Summoning symbol. The third advances the Elder One and the discards are shuffled back " +
                "into the Mythos deck; resolve the Elder One's advance effects first, then the episode's.",
            "Once summoned, its dice are all the dice on its revealed Stage cards, and every revealed stage's effects stay in play.",
            "Each stage falls when it takes its printed health in wounds. Killing the Final stage wins the game.",
        ).forEach { Text("• $it", color = p.muted, style = MaterialTheme.typography.bodyMedium, modifier = Modifier.padding(horizontal = 8.dp, vertical = 2.dp)) }
    }
}

/** Picks an Elder One or episode by box, or takes a typed name; Clear unsets it. */
@Composable
internal fun StoryPickerDialog(title: String, groups: List<StoryGroup>, current: String?, onSave: (String?) -> Unit, onDismiss: () -> Unit) {
    val p = palette
    var query by rememberSaveable { mutableStateOf("") }
    val search = query.trim()
    val matches = groups.mapNotNull { group ->
        group.names.filter { search.isEmpty() || it.contains(search, ignoreCase = true) }.takeIf { it.isNotEmpty() }?.let { group.title to it }
    }
    val typedNew = search.isNotEmpty() && groups.none { g -> g.names.any { it.equals(search, ignoreCase = true) } }
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(title) },
        text = {
            Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                OutlinedTextField(query, { query = it.take(ElderOneTrack.MAX_NAME) }, label = { Text("Search or type a name") },
                    singleLine = true, modifier = Modifier.fillMaxWidth())
                Column(Modifier.weight(1f, fill = false).verticalScroll(rememberScrollState()), verticalArrangement = Arrangement.spacedBy(2.dp)) {
                    if (typedNew) StoryRow("Use “$search”", false) { onSave(search) }
                    if (matches.isEmpty() && !typedNew) Text("Nothing matches.", color = p.muted)
                    matches.forEach { (group, names) ->
                        Text(group, color = p.muted, style = MaterialTheme.typography.labelLarge,
                            modifier = Modifier.padding(top = 8.dp).semantics { heading() })
                        names.forEach { name -> StoryRow(name, name == current) { onSave(name) } }
                    }
                }
                Text("From the community Death May Die wiki. Any name can be typed in.", color = p.faint, style = MaterialTheme.typography.bodySmall)
            }
        },
        confirmButton = { TextButton(onDismiss) { Text("Cancel") } },
        dismissButton = { if (current != null) TextButton({ onSave(null) }) { Text("Clear") } },
    )
}

@Composable
private fun StoryRow(name: String, chosen: Boolean, onPick: () -> Unit) {
    val p = palette
    Text(if (chosen) "✓ $name" else name, color = p.text, style = MaterialTheme.typography.bodyLarge,
        fontWeight = if (chosen) FontWeight.SemiBold else FontWeight.Normal,
        modifier = Modifier.fillMaxWidth().clip(RoundedCornerShape(8.dp))
            .background(if (chosen) p.accentSoft else Color.Transparent)
            .clickable(onClickLabel = "Choose $name", role = Role.Button, onClick = onPick)
            .semantics { selected = chosen; contentDescription = name }
            .padding(horizontal = 8.dp, vertical = 10.dp))
}
