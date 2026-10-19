package com.turnhub.android.ui.tablet

import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawingPadding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import com.turnhub.android.domain.DeathMayDieStory
import com.turnhub.android.domain.InvestigatorCardSlot
import com.turnhub.android.domain.TablePlayer
import com.turnhub.android.domain.TableSummary
import com.turnhub.android.ui.components.AccentButton
import com.turnhub.android.ui.components.BrassCard
import com.turnhub.android.ui.components.Eyebrow
import com.turnhub.android.ui.components.ToneButton
import com.turnhub.android.ui.components.colorInk
import com.turnhub.android.ui.components.displayName
import com.turnhub.android.ui.components.playerColor
import com.turnhub.android.ui.components.tableBackground
import com.turnhub.android.ui.theme.palette

/**
 * Optional pass-the-device setup (owner, 2026-10-19): after the game is chosen, the device
 * goes round the table and each player makes their own choices (Death May Die: investigator,
 * sanity condition and relic; other games: tile card or commander, and tile color). It ends
 * on a confirm screen that starts the game or returns to the setup menu. Every choice goes
 * through the lobby's own [TableLobbyActions], so nothing here is a second way to set up.
 */
@Composable
internal fun GuidedSetupDialog(
    summary: TableSummary,
    actions: TableLobbyActions,
    startReady: Boolean,
    startHint: String?,
    onStart: () -> Unit,
    onClose: () -> Unit,
) {
    val players = summary.players.sortedBy { it.playerNumber }
    // Death May Die opens on the table's shared Story choices, before the device goes round.
    val story = summary.settings.profile.deathMayDie && actions.elderOne != null && actions.episode != null
    val first = if (story) 1 else 0
    // Steps: the Story step, per player a hand-off screen then their choices, and the confirm screen last.
    var step by rememberSaveable { mutableStateOf(0) }
    var backToConfirm by rememberSaveable { mutableStateOf(false) }
    val confirm = first + players.size * 2
    val at = step.coerceIn(0, confirm)
    val next = { step = if (backToConfirm) confirm else at + 1; backToConfirm = false }
    // Edge to edge like the app behind it, and wide enough to use a tablet's width.
    Dialog(onClose, DialogProperties(usePlatformDefaultWidth = false, dismissOnClickOutside = false, decorFitsSystemWindows = false)) {
        val p = palette
        Box(Modifier.fillMaxSize().tableBackground(p).safeDrawingPadding(), contentAlignment = Alignment.TopCenter) {
            Column(Modifier.widthIn(max = 1000.dp).fillMaxWidth().verticalScroll(rememberScrollState()).padding(16.dp),
                verticalArrangement = Arrangement.spacedBy(14.dp)) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Text(
                        when {
                            at < first -> "Guided setup · the story"
                            at < confirm -> "Guided setup · player ${(at - first) / 2 + 1} of ${players.size}"
                            else -> "Guided setup · confirm"
                        },
                        color = p.muted, style = MaterialTheme.typography.labelLarge, modifier = Modifier.weight(1f),
                    )
                    TextButton(onClose) { Text("Exit") }
                }
                val player = players.getOrNull((at - first) / 2)
                when {
                    at < first -> StoryStep(summary, actions, back = backToConfirm, onDone = next)
                    player == null || at >= confirm -> ConfirmStep(summary, players, startReady, startHint,
                        onEditStory = if (story) ({ step = 0; backToConfirm = true }) else null,
                        onEdit = { index -> step = first + index * 2 + 1; backToConfirm = true }, onStart = onStart, onBack = onClose)
                    (at - first) % 2 == 0 -> HandOffStep(player, onReady = { step = at + 1 }, onSkip = { step = at + 2 })
                    else -> PlayerStep(summary, player, actions, last = at + 1 == confirm || backToConfirm, onDone = next)
                }
            }
        }
    }
}

/** Death May Die: the Elder One and episode the whole table plays, chosen once before the device goes round. */
@Composable
private fun StoryStep(summary: TableSummary, actions: TableLobbyActions, back: Boolean, onDone: () -> Unit) {
    val p = palette
    val elder = summary.elder
    var picking by rememberSaveable { mutableStateOf<String?>(null) }
    Text("The story", color = p.text, style = MaterialTheme.typography.headlineSmall, modifier = Modifier.semantics { heading() })
    Text("Choose the Elder One and episode from the box you are playing. Both are optional, and the Elder One tab can change them later.",
        color = p.muted)
    BrassCard {
        Eyebrow("Elder One")
        Text(elder?.elderOne ?: "Not chosen", color = if (elder?.elderOne == null) p.muted else p.text,
            style = MaterialTheme.typography.titleLarge, fontWeight = FontWeight.SemiBold)
        ToneButton(if (elder?.elderOne == null) "Choose the Elder One" else "Change the Elder One", { picking = "elder" }, Modifier.fillMaxWidth())
    }
    BrassCard {
        Eyebrow("Episode")
        Text(elder?.episode ?: "Not chosen", color = if (elder?.episode == null) p.muted else p.text,
            style = MaterialTheme.typography.titleLarge, fontWeight = FontWeight.SemiBold)
        ToneButton(if (elder?.episode == null) "Choose the episode" else "Change the episode", { picking = "episode" }, Modifier.fillMaxWidth())
    }
    AccentButton(if (back) "Done: review the table" else "Next: pass the device round", onDone, Modifier.fillMaxWidth())
    when (picking) {
        "elder" -> actions.elderOne?.let { save ->
            StoryPickerDialog("Elder One", DeathMayDieStory.elderOnes, elder?.elderOne, { save(it); picking = null }) { picking = null }
        }
        "episode" -> actions.episode?.let { save ->
            StoryPickerDialog("Episode", DeathMayDieStory.episodes, elder?.episode, { save(it); picking = null }) { picking = null }
        }
    }
}

/** A big "pass to" card, so the next player knows the device is theirs. */
@Composable
private fun HandOffStep(player: TablePlayer, onReady: () -> Unit, onSkip: () -> Unit) {
    val p = palette
    val hue = playerColor(player.color, p.avatarColor(player.playerNumber))
    BrassCard(highlight = hue) {
        Column(Modifier.fillMaxWidth().padding(vertical = 24.dp), horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.spacedBy(14.dp)) {
            Text("Pass the device to", color = p.muted, style = MaterialTheme.typography.titleMedium)
            Box(Modifier.size(72.dp).background(hue, CircleShape), contentAlignment = Alignment.Center) {
                Text("${player.playerNumber}", color = colorInk(hue), style = MaterialTheme.typography.headlineMedium, fontWeight = FontWeight.Black)
            }
            Text(player.label, color = p.text, style = MaterialTheme.typography.displaySmall, fontWeight = FontWeight.Black,
                textAlign = TextAlign.Center, modifier = Modifier.semantics { heading() })
            AccentButton("I'm ${player.label}", onReady, Modifier.fillMaxWidth())
            TextButton(onSkip) { Text("Skip ${player.label}") }
        }
    }
}

/** One player's own choices for this game, then on to the next player. */
@Composable
private fun PlayerStep(summary: TableSummary, player: TablePlayer, actions: TableLobbyActions, last: Boolean, onDone: () -> Unit) {
    val p = palette
    val profile = summary.settings.profile
    var choosingInvestigator by rememberSaveable { mutableStateOf(false) }
    var namingCard by rememberSaveable { mutableStateOf(false) }
    Text("${player.label}'s choices", color = p.text, style = MaterialTheme.typography.headlineSmall,
        modifier = Modifier.semantics { heading() })
    val investigators = profile.deathMayDie && actions.investigator != null
    if (investigators) {
        BrassCard {
            Eyebrow("Investigator")
            val art = rememberInvestigatorArt(player.investigator)
            // The whole portrait beside the name: a full-width strip cropped heads off.
            Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(16.dp)) {
                art?.let {
                    Image(it.image, "${player.investigator}'s portrait",
                        Modifier.height(220.dp).aspectRatio(it.image.width.toFloat() / it.image.height.coerceAtLeast(1))
                            .clip(RoundedCornerShape(14.dp)),
                        contentScale = ContentScale.Fit)
                }
                Column(Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(12.dp)) {
                    Text(player.investigator ?: "Not chosen yet", color = if (player.investigator == null) p.muted else p.text,
                        style = MaterialTheme.typography.titleLarge, fontWeight = FontWeight.SemiBold)
                    ToneButton(if (player.investigator == null) "Choose your investigator" else "Change investigator",
                        { choosingInvestigator = true }, Modifier.fillMaxWidth())
                }
            }
        }
        if (choosingInvestigator) actions.investigator?.let { save ->
            val taken = summary.players.filter { it.playerNumber != player.playerNumber }
                .mapNotNull { other -> other.investigator?.let { it to other.label } }.toMap()
            InvestigatorPickerDialog(player, taken, { name -> save(player, name); choosingInvestigator = false }) { choosingInvestigator = false }
        }
        BrassCard {
            Eyebrow(if (summary.useRelics) "Sanity condition and relic" else "Sanity condition")
            if (player.investigator == null) {
                Text("Choose your investigator first: a new investigator starts with a fresh dashboard.", color = p.muted)
            } else actions.investigatorCard?.let { assign ->
                InvestigatorCards(player, summary.bootId, true, compact = false, references = summary.investigatorReferences,
                    useRelics = summary.useRelics, drawTwo = summary.drawTwoCards,
                    onDeal = actions.dealInvestigatorCards?.let { deal -> { slot -> deal(player, slot) } },
                    reservedCardIds = summary.players.flatMap { other -> other.dashboard.cardOffers.values.flatten() +
                        InvestigatorCardSlot.entries.mapNotNull { other.dashboard.card(it)?.catalogueId } }.toSet(),
                    onAssign = { slot, card -> assign(player, slot, card) })
            }
        }
    } else if (actions.commander != null) {
        BrassCard {
            Eyebrow(if (profile.commanderRules) "Commander" else "Tile card")
            val art = rememberCommanderArt(player.commander, player.commanderArt)
            art?.let {
                Image(it.image, "${it.cardName} art", Modifier.fillMaxWidth().height(180.dp).clip(RoundedCornerShape(14.dp)),
                    contentScale = ContentScale.Crop)
                Text("Art: ${it.artist} (${it.source})", color = p.faint, style = MaterialTheme.typography.labelSmall)
            }
            Text(player.commander ?: if (profile.commanderRules) "No commander named yet" else "Optional: a card's art for your tile",
                color = if (player.commander == null) p.muted else p.text, style = MaterialTheme.typography.titleMedium)
            ToneButton(when {
                player.commander == null -> if (profile.commanderRules) "Name your commander" else "Choose a tile card"
                player.namedCommander != null -> "Change commander"
                else -> "Change tile card"
            }, { namingCard = true }, Modifier.fillMaxWidth())
        }
        if (namingCard) actions.commander?.let { save ->
            CommanderPickerDialog(player, profile.commanderRules, com.turnhub.android.data.TileCardGame.forProfile(profile),
                { name, art, cardOnly -> save(player, name, art, cardOnly); namingCard = false }) { namingCard = false }
        }
    }
    actions.background?.takeIf { actions.canEditBackground(player) }?.let { edit ->
        BrassCard {
            Eyebrow("Your tile")
            ToneButton("Tile color & photo", { edit(player) }, Modifier.fillMaxWidth())
        }
    }
    val missing = missingFor(summary, player)
    if (missing.isNotEmpty()) Text("Still needed before the game can start: ${missing.joinToString(", ")}.", color = p.warn)
    AccentButton(if (last) "Done: review the table" else "Done: pass it on", onDone, Modifier.fillMaxWidth())
}

/** Everyone's choices on one screen, each with Edit, then Start or back to the setup menu. */
@Composable
private fun ConfirmStep(
    summary: TableSummary,
    players: List<TablePlayer>,
    startReady: Boolean,
    startHint: String?,
    onEditStory: (() -> Unit)?,
    onEdit: (Int) -> Unit,
    onStart: () -> Unit,
    onBack: () -> Unit,
) {
    val p = palette
    val settings = summary.settings
    Text("Ready to play?", color = p.text, style = MaterialTheme.typography.headlineMedium, fontWeight = FontWeight.Black,
        modifier = Modifier.semantics { heading() })
    Text(settings.profile.displayName() + if (settings.twoHeadedGiant) " · Two-Headed Giant" else "", color = p.muted,
        style = MaterialTheme.typography.titleMedium)
    onEditStory?.let { edit ->
        BrassCard {
            Eyebrow("The story")
            Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                Column(Modifier.weight(1f)) {
                    Text("Elder One: ${summary.elder?.elderOne ?: "not chosen"}", color = p.text, style = MaterialTheme.typography.bodyLarge)
                    Text("Episode: ${summary.elder?.episode ?: "not chosen"}", color = p.text, style = MaterialTheme.typography.bodyLarge)
                }
                TextButton(edit) { Text("Edit") }
            }
        }
    }
    BrassCard {
        Eyebrow("At the table")
        players.forEachIndexed { index, player ->
            val hue = playerColor(player.color, p.avatarColor(player.playerNumber))
            Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                Box(Modifier.size(30.dp).background(hue, CircleShape), contentAlignment = Alignment.Center) {
                    Text("${player.playerNumber}", color = colorInk(hue), style = MaterialTheme.typography.labelLarge)
                }
                Column(Modifier.weight(1f)) {
                    Text(player.label, color = p.text, style = MaterialTheme.typography.bodyLarge, fontWeight = FontWeight.SemiBold,
                        maxLines = 1, overflow = TextOverflow.Ellipsis)
                    choicesFor(summary, player).takeIf { it.isNotEmpty() }?.let {
                        Text(it.joinToString(" · "), color = p.muted, style = MaterialTheme.typography.bodySmall)
                    }
                    missingFor(summary, player).takeIf { it.isNotEmpty() }?.let {
                        Text("Needs: ${it.joinToString(", ")}", color = p.bad, style = MaterialTheme.typography.bodySmall)
                    }
                }
                TextButton({ onEdit(index) }) { Text("Edit") }
            }
        }
    }
    if (!startReady) startHint?.let { Text(it, color = p.muted) }
    AccentButton("Start the game", onStart, Modifier.fillMaxWidth(), enabled = startReady)
    ToneButton("Back to the setup menu", onBack, Modifier.fillMaxWidth())
}

/** What a player has chosen, in a few words for the confirm screen. */
internal fun choicesFor(summary: TableSummary, player: TablePlayer): List<String> = buildList {
    if (summary.settings.profile.deathMayDie) {
        player.investigator?.let(::add)
        InvestigatorCardSlot.entries.filter { it != InvestigatorCardSlot.RELIC || summary.useRelics }
            .forEach { slot -> player.dashboard.card(slot)?.let { add(it.name) } }
    } else player.commander?.let(::add)
}

/** What a player still has to choose before Start, in the order it is asked for. */
internal fun missingFor(summary: TableSummary, player: TablePlayer): List<String> = buildList {
    if (!summary.settings.profile.deathMayDie) return@buildList
    if (player.investigator == null) add("investigator")
    if (player.dashboard.card(InvestigatorCardSlot.SANITY_CONDITION) == null) add("sanity condition")
    if (summary.useRelics && player.dashboard.card(InvestigatorCardSlot.RELIC) == null) add("relic")
}
