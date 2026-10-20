package com.turnhub.android.ui.tablet

import android.content.Context
import android.graphics.BitmapFactory
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.FilterChip
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateMapOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.produceState
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.semantics.LiveRegionMode
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.liveRegion
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import com.turnhub.android.data.PlaneInfo
import com.turnhub.android.data.ScryfallPlanes
import com.turnhub.android.protocol.TableState
import com.turnhub.android.standalone.PlanarCard
import com.turnhub.android.standalone.PlanarDeckSource
import com.turnhub.android.standalone.PlanarFace
import com.turnhub.android.standalone.Planechase
import com.turnhub.android.standalone.StandaloneGame
import com.turnhub.android.standalone.StandaloneState
import com.turnhub.android.standalone.StandaloneTable
import com.turnhub.android.ui.components.AccentButton
import com.turnhub.android.ui.components.BrassCard
import com.turnhub.android.ui.components.DisclosureCard
import com.turnhub.android.ui.components.Tone
import com.turnhub.android.ui.components.ToneButton
import com.turnhub.android.ui.theme.palette
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import java.io.File

/** The planes on this phone, downloaded from Scryfall once, and their decoded art; one copy for the app. */
internal object PlaneLibrary {
    var arrivalEffect by mutableStateOf<PlanarFace?>(null)
    private var source: ScryfallPlanes? = null
    private val art = mutableStateMapOf<String, ImageBitmap>()
    /** The downloaded cards by id; null until the saved list is read or downloaded. */
    var cards by mutableStateOf<Map<String, PlaneInfo>?>(null)
        private set
    var downloading by mutableStateOf(false)
        private set
    var problem by mutableStateOf<String?>(null)
        private set

    fun source(context: Context): ScryfallPlanes = source ?: run {
        val app = context.applicationContext
        ScryfallPlanes(File(app.filesDir, "planes"), File(app.cacheDir, "plane_art"), scryfallUserAgent(app)).also { source = it }
    }

    /** What a digital deck is built from: every downloaded card, or nothing yet. */
    fun pool(context: Context): List<PlanarCard> = known(context)?.values?.map { it.card }.orEmpty()

    fun known(context: Context): Map<String, PlaneInfo>? = cards ?: source(context).saved()?.associateBy { it.id }?.also { cards = it }

    suspend fun download(context: Context) {
        if (downloading) return
        downloading = true
        problem = null
        runCatching { source(context).download() }
            .onSuccess { list -> cards = list.associateBy { it.id } }
            .onFailure { problem = "Couldn't download the planes. Check the internet connection and try again." }
        downloading = false
    }

    suspend fun art(context: Context, card: PlaneInfo): ImageBitmap? = art[card.id] ?: run {
        val bytes = source(context).art(card) ?: return null
        val bitmap = BitmapFactory.decodeByteArray(bytes, 0, bytes.size)?.asImageBitmap() ?: return null
        // Browsing a whole collection must not retain every decoded image in RAM.
        if (art.size >= 12) art.keys.firstOrNull()?.let { art.remove(it) }
        art[card.id] = bitmap
        bitmap
    }
}

/**
 * Downloads the planes the first time a table plays Planechase with the
 * digital deck, from whichever screen turned it on (Game setup or the Planes tab).
 */
@Composable
internal fun PlanesDownloadWhenNeeded(planar: Planechase) {
    val context = LocalContext.current
    LaunchedEffect(planar.enabled, planar.source) {
        if (planar.enabled && planar.source == PlanarDeckSource.DIGITAL && PlaneLibrary.known(context) == null &&
            PlaneLibrary.problem == null) PlaneLibrary.download(context)
    }
}

/** How the digital deck stands on this phone, in a few words for setup; null once it's here. */
internal fun planesDownloadNote(): String? = when {
    PlaneLibrary.downloading -> "Downloading the planes…"
    PlaneLibrary.cards != null -> null
    PlaneLibrary.problem != null -> "The planes didn't download. Retry in the Planes tab, or a game uses your own cards."
    else -> "The planes download the first time. Until then, a game uses your own cards."
}

/**
 * The Planes tab (Magic games on this device): Planechase setup before a game,
 * then the face-up plane, the planar die and the deck tools during one. It acts
 * for the player whose turn it is; the engine is [Planechase].
 */
@Composable
fun PlanesTab(state: StandaloneState, table: StandaloneTable) {
    val context = LocalContext.current
    val game = state.game
    val planar = game.planechase
    val playing = game.state == TableState.RUNNING || game.state == TableState.PAUSED
    val cards = PlaneLibrary.cards
    LaunchedEffect(Unit) { PlaneLibrary.known(context) }
    PlanesDownloadWhenNeeded(planar)
    Column(
        Modifier.widthIn(max = 720.dp).fillMaxWidth().verticalScroll(rememberScrollState()).padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(14.dp),
    ) {
        if (playing && planar.enabled) {
            PlanarPlay(game, PlanarControls.of(table), cards) { LateDeck(table, cards) }
        } else {
            if (playing) BrassCard { Text("This game isn't using Planechase. Turn it on here before the next game.", color = palette.muted) }
            PlanarSetup(state, table, cards)
        }
        PlanarCredit()
    }
}

@Composable
private fun PlanarSetup(state: StandaloneState, table: StandaloneTable, cards: Map<String, PlaneInfo>?) {
    val p = palette
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    val game = state.game
    val planar = game.planechase
    val lobby = game.state == TableState.LOBBY
    BrassCard {
        Text("Planechase", color = p.text, style = MaterialTheme.typography.titleLarge, modifier = Modifier.semantics { heading() })
        Text("Plane cards and the planar die join the game. TurnHub shuffles the planar deck, rolls the die and counts " +
            "what each roll costs. Players still do what the cards say.", color = p.muted)
        SetupSwitch("Play Planechase", if (lobby) null else "Change this in the lobby, before the game starts.",
            planar.enabled, lobby, table::setPlanechase)
        if (planar.enabled) {
            Text("Planar deck", color = p.muted, style = MaterialTheme.typography.titleSmall)
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                FilterChip(planar.source == PlanarDeckSource.DIGITAL, { table.setPlanarSource(PlanarDeckSource.DIGITAL) },
                    label = { Text("Digital deck") }, enabled = lobby)
                FilterChip(planar.source == PlanarDeckSource.PHYSICAL, { table.setPlanarSource(PlanarDeckSource.PHYSICAL) },
                    label = { Text("Physical cards") }, enabled = lobby)
            }
            if (planar.source == PlanarDeckSource.PHYSICAL) {
                Text("Shuffle your planar deck and turn over the first plane. TurnHub rolls the die, counts the cost " +
                    "and tells you when to turn over the next one.", color = p.muted)
            } else {
                SetupSwitch("Include phenomena", "Up to two for each player, as the official rules allow.",
                    planar.phenomena, lobby, table::setPlanarPhenomena)
                if (cards != null) PlanarDeckPicker(state, table, cards.values.toList(), lobby)
                val selected = cards?.values?.filter { planar.selection.cardIds == null || it.id in planar.selection.cardIds.orEmpty() }
                val planes = selected?.count { !it.phenomenon } ?: 0
                val phenomena = selected?.count { it.phenomenon } ?: 0
                when {
                    PlaneLibrary.downloading -> Text("Downloading the planes…", color = p.muted,
                        modifier = Modifier.semantics { liveRegion = LiveRegionMode.Polite })
                    cards == null -> {
                        Text(PlaneLibrary.problem ?: "The planes aren't on this phone yet.", color = if (PlaneLibrary.problem != null) p.bad else p.muted)
                        AccentButton("Download the planes", { scope.launch { PlaneLibrary.download(context) } }, Modifier.fillMaxWidth())
                        Text("Without them, a game uses your own cards.", color = p.faint, style = MaterialTheme.typography.bodySmall)
                    }
                    else -> {
                        Text("Every game shuffles $planes selected planes" + if (planar.phenomena) " and some of the $phenomena phenomena." else ".", color = p.muted)
                        ToneButton("Refresh library", { scope.launch { PlaneLibrary.download(context) } }, Modifier.fillMaxWidth())
                    }
                }
            }
        }
    }
    DisclosureCard("Planechase rules") {
        listOf(
            "The planar die has six faces: one Planeswalker symbol, one chaos symbol and four blanks (901.3a).",
            "During your main phase, with nothing on the stack, you may roll the planar die. Your first roll each turn costs {0}, then {1} more for each roll before it (901.9).",
            "Chaos: the plane's chaos ability triggers (311.7). Planeswalk: the plane goes to the bottom of the deck and the next one turns face up (701.31).",
            "A phenomenon happens once when it turns face up, then you planeswalk away from it (312.5, 704.6f).",
            "The player whose turn it is controls the planes (901.6). This game uses one shared planar deck (901.15).",
        ).forEach { Text("• $it", color = p.text, style = MaterialTheme.typography.bodyMedium) }
    }
}

/** A digital game that started before the planes were on this phone: switch to the digital deck once they are. */
@Composable
private fun LateDeck(table: StandaloneTable, cards: Map<String, PlaneInfo>?) {
    val p = palette
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    Text("The digital deck wasn't on this phone when the game started, so the table is using its own cards.", color = p.muted)
    when {
        cards != null && cards.values.any { !it.phenomenon } ->
            AccentButton("Switch to digital deck", table::dealPlanarDeck, Modifier.fillMaxWidth())
        PlaneLibrary.downloading -> Text("Downloading the planes…", color = p.muted,
            modifier = Modifier.semantics { liveRegion = LiveRegionMode.Polite })
        else -> ToneButton("Download the planes", { scope.launch { PlaneLibrary.download(context) } }, Modifier.fillMaxWidth())
    }
}

/**
 * What the Planes tab's buttons do: the host's own table, or a guest sending them to the
 * host. Either way they act for the active player. [enabled] is false while a guest can't send.
 */
internal class PlanarControls(
    val roll: (fromEffect: Boolean) -> Unit,
    val planeswalk: () -> Unit,
    val resolvePhenomenon: () -> Unit,
    val chaosEnsues: () -> Unit,
    val topToBottom: () -> Unit,
    val changeCounter: (card: String, delta: Int) -> Unit,
    /** Planes that reverse turn order (owner, 2026-10-20). */
    val reverseTurnOrder: (on: Boolean) -> Unit,
    val enabled: Boolean = true,
) {
    companion object {
        fun of(table: StandaloneTable) = PlanarControls({ table.rollPlanarDie(fromEffect = it) }, table::planeswalk,
            table::resolvePhenomenon, table::chaosEnsues, table::planarTopToBottom, table::changePlaneCounter,
            table::setTurnOrderReversed)
    }
}

/**
 * A joined device's Planes screen: the host's face-up plane, die and counters, live from its
 * snapshot, with the same buttons as the host's tab. The card text and art come from Scryfall
 * on this device, like the host's; the host keeps the deck and rolls the die.
 */
@Composable
internal fun SharedPlanes(game: StandaloneGame, controls: PlanarControls, modifier: Modifier = Modifier) {
    val context = LocalContext.current
    val planar = game.planechase
    val cards = PlaneLibrary.cards
    val scope = rememberCoroutineScope()
    LaunchedEffect(Unit) { PlaneLibrary.known(context) }
    PlanesDownloadWhenNeeded(planar)
    Column(
        modifier.widthIn(max = 720.dp).fillMaxWidth().verticalScroll(rememberScrollState()).padding(vertical = 8.dp),
        verticalArrangement = Arrangement.spacedBy(14.dp),
    ) {
        when {
            !planar.enabled -> BrassCard { Text("This game isn't using Planechase. The host turns it on in Game setup.", color = palette.muted) }
            game.state == TableState.LOBBY -> BrassCard { Text("The planes appear when the host starts the game.", color = palette.muted) }
            game.state == TableState.GAME_OVER -> BrassCard { Text("Game over. The planes return with the next game.", color = palette.muted) }
            else -> {
                if (!planar.physical && cards == null) BrassCard {
                    Text(if (PlaneLibrary.problem != null) "The plane cards didn't download to this device. Check its internet, then retry."
                        else "Downloading the plane cards to this device…", color = palette.muted)
                    if (PlaneLibrary.problem != null) ToneButton("Retry download", { scope.launch { PlaneLibrary.download(context) } })
                }
                PlanarPlay(game, controls, cards, lateDeck = null)
            }
        }
        PlanarCredit()
    }
}

@Composable
private fun PlanarPlay(game: StandaloneGame, controls: PlanarControls, cards: Map<String, PlaneInfo>?,
    lateDeck: (@Composable () -> Unit)?) {
    val p = palette
    val planar = game.planechase
    val running = game.state == TableState.RUNNING && controls.enabled
    val controller = game.active?.let { game.players.getOrNull(it)?.name }
    // Chaos and physical planeswalks show for a few seconds after they happen (not when the tab opens).
    var seenChaos by remember { mutableStateOf(planar.chaos) }
    var seenWalks by remember { mutableStateOf(planar.planeswalks) }
    var chaosNow by remember { mutableStateOf(PlaneLibrary.arrivalEffect == PlanarFace.CHAOS) }
    var walkNow by remember { mutableStateOf(PlaneLibrary.arrivalEffect == PlanarFace.PLANESWALK) }
    LaunchedEffect(Unit) {
        PlaneLibrary.arrivalEffect = null
        delay(5000)
        chaosNow = false; walkNow = false
    }
    if (chaosNow) BrassCard(highlight = p.warn) {
        Text("Chaos ensues!", color = p.warn, style = MaterialTheme.typography.headlineMedium)
        Text("Resolve the plane's chaos ability.", color = p.text)
    }
    if (walkNow) BrassCard(highlight = p.accent) {
        Text("Planeswalk!", color = p.accent, style = MaterialTheme.typography.headlineMedium)
        Text("Resolve the new face-up card.", color = p.text)
    }
    LaunchedEffect(planar.chaos) {
        if (planar.chaos > seenChaos) { chaosNow = true; delay(5_000); chaosNow = false }
        seenChaos = planar.chaos
    }
    LaunchedEffect(planar.planeswalks) {
        if (planar.planeswalks > seenWalks) { walkNow = true; delay(5_000); walkNow = false }
        seenWalks = planar.planeswalks
    }

    Text(if (game.state == TableState.PAUSED) "Paused. Resume the game to roll or planeswalk."
        else "${controller ?: "The active player"} controls the planes.", color = p.muted)

    if (planar.physical) {
        BrassCard(highlight = if (walkNow) p.accent else null) {
            Text("Your own cards", color = p.text, style = MaterialTheme.typography.titleLarge)
            if (planar.source == PlanarDeckSource.DIGITAL) lateDeck?.invoke()
            Text(if (walkNow) "Planeswalk! Put the plane on the bottom and turn over the next one."
                else "Keep the face-up plane where everyone can read it.", color = if (walkNow) p.text else p.muted,
                modifier = Modifier.semantics { liveRegion = LiveRegionMode.Polite })
            PlaneCounters(planar.counters[Planechase.PHYSICAL_PLANE] ?: 0, controls.enabled) { controls.changeCounter(Planechase.PHYSICAL_PLANE, it) }
        }
    } else {
        planar.faceUp.forEach { card ->
            PlaneCardView(cards?.get(card.id), card, planar.counters[card.id] ?: 0, chaosNow, controls.enabled) { controls.changeCounter(card.id, it) }
        }
    }

    BrassCard {
        val roll = planar.lastRoll
        if (roll != null) {
            val who = game.players.getOrNull(roll.player)?.name ?: "Player ${roll.player + 1}"
            Text(dieFace(roll.face), color = when (roll.face) {
                PlanarFace.BLANK -> p.muted
                PlanarFace.CHAOS -> p.warn
                PlanarFace.PLANESWALK -> p.accent
            }, style = MaterialTheme.typography.headlineMedium, fontWeight = FontWeight.Bold, textAlign = TextAlign.Center,
                modifier = Modifier.fillMaxWidth().semantics { liveRegion = LiveRegionMode.Polite })
            Text("$who rolled" + when {
                roll.fromEffect -> " for a card's effect."
                else -> ", paying {${roll.cost}}."
            }, color = p.muted, textAlign = TextAlign.Center, modifier = Modifier.fillMaxWidth())
        }
        if (planar.phenomenonPending) {
            AccentButton("Done: planeswalk away", controls.resolvePhenomenon, Modifier.fillMaxWidth(), enabled = running)
        } else {
            val cost = game.active?.let { planar.cost(it) } ?: 0
            AccentButton("Roll the planar die: {$cost}", { controls.roll(false) }, Modifier.fillMaxWidth(), enabled = running)
        }
    }

    DisclosureCard("When a card says so") {
        ToneButton("Roll for a card (free)", { controls.roll(true) }, Modifier.fillMaxWidth(),
            enabled = running && !planar.phenomenonPending)
        ToneButton("Planeswalk", controls.planeswalk, Modifier.fillMaxWidth(), enabled = running)
        ToneButton("Chaos ensues", controls.chaosEnsues, Modifier.fillMaxWidth(), enabled = running && !planar.phenomenonPending)
        ToneButton(if (game.turnOrderReversed) "↻ Turn order back to clockwise" else "↺ Reverse turn order",
            { controls.reverseTurnOrder(!game.turnOrderReversed) }, Modifier.fillMaxWidth(), enabled = running)
        if (!planar.physical) {
            ToneButton("Put the top card on the bottom", controls.topToBottom, Modifier.fillMaxWidth(), enabled = running)
        }
    }
}

/** The die's faces in words, with a mark that isn't Wizards' symbol: never colour or a glyph alone. */
private fun dieFace(face: PlanarFace): String = when (face) {
    PlanarFace.BLANK -> "Blank"
    PlanarFace.CHAOS -> "✷ Chaos"
    PlanarFace.PLANESWALK -> "➜ Planeswalk"
}

@Composable
private fun PlaneCardView(info: PlaneInfo?, card: PlanarCard, counters: Int, chaosNow: Boolean, enabled: Boolean, onCounter: (Int) -> Unit) {
    val p = palette
    val context = LocalContext.current
    BrassCard(highlight = if (chaosNow) p.warn else null) {
        if (info == null) {
            Text(if (card.phenomenon) "A phenomenon" else "A plane", color = p.text, style = MaterialTheme.typography.titleLarge)
            Text("This card isn't on the phone any more. Download the planes again from setup.", color = p.muted)
        } else {
            val art by produceState<ImageBitmap?>(null, info.id) { value = PlaneLibrary.art(context, info) }
            art?.let {
                Image(it, contentDescription = null, contentScale = ContentScale.Crop,
                    modifier = Modifier.fillMaxWidth().aspectRatio(4f / 3f).clip(RoundedCornerShape(12.dp)))
            }
            Text(info.name, color = p.text, style = MaterialTheme.typography.headlineSmall, fontWeight = FontWeight.Bold,
                modifier = Modifier.semantics { heading() })
            Text(info.typeLine, color = p.muted, style = MaterialTheme.typography.titleSmall)
            if (chaosNow) Text("Chaos ensues!", color = p.warn, style = MaterialTheme.typography.titleMedium, fontWeight = FontWeight.Bold,
                modifier = Modifier.semantics { liveRegion = LiveRegionMode.Polite })
            info.text.split('\n').filter { it.isNotBlank() }.forEach { line ->
                val chaos = line.contains("chaos ensues", ignoreCase = true)
                Text(line, color = p.text, style = MaterialTheme.typography.bodyLarge,
                    fontWeight = if (chaos && chaosNow) FontWeight.Bold else FontWeight.Normal,
                    modifier = if (chaos) Modifier.fillMaxWidth().background(p.surface2, RoundedCornerShape(8.dp)).padding(8.dp) else Modifier)
            }
            Text("Art by ${info.artist}. Card from Scryfall.", color = p.faint, style = MaterialTheme.typography.bodySmall)
        }
        if (!card.phenomenon) PlaneCounters(counters, enabled, onCounter)
    }
}

@Composable
private fun PlaneCounters(count: Int, enabled: Boolean, onChange: (Int) -> Unit) {
    val p = palette
    Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(10.dp)) {
        Text("Counters on this plane", color = p.muted, modifier = Modifier.weight(1f))
        ToneButton("−", { onChange(-1) }, enabled = enabled && count > 0)
        Text("$count", color = p.text, style = MaterialTheme.typography.titleLarge, fontWeight = FontWeight.Bold)
        ToneButton("+", { onChange(1) }, tone = Tone.ACCENT, enabled = enabled)
    }
}

@Composable
private fun PlanarCredit() {
    Text("Card names, text and art come from Scryfall and stay on this phone. TurnHub is unofficial Fan Content " +
        "permitted under the Fan Content Policy. Not approved or endorsed by Wizards. Portions of the materials used " +
        "are property of Wizards of the Coast. ©Wizards of the Coast LLC.",
        color = palette.faint, style = MaterialTheme.typography.bodySmall)
}
