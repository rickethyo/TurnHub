package com.turnhub.android.ui.home

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.clip
import androidx.compose.ui.semantics.LiveRegionMode
import androidx.compose.ui.semantics.liveRegion
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.turnhub.android.data.SeatClaim
import com.turnhub.android.domain.ControllerHandle
import com.turnhub.android.domain.TableClock
import com.turnhub.android.domain.TablePlayer
import com.turnhub.android.domain.TableSummary
import com.turnhub.android.domain.seatLabel
import com.turnhub.android.protocol.TableState
import com.turnhub.android.ui.components.BrassCard
import com.turnhub.android.ui.components.Eyebrow
import com.turnhub.android.ui.components.PlayerAvatar
import com.turnhub.android.ui.components.StatusBadge
import com.turnhub.android.ui.components.Tone
import com.turnhub.android.ui.components.ToneButton
import com.turnhub.android.ui.theme.palette

/** "Use this seat" on a Sigil seat, as in the portal; Atlas asks that Sigil to confirm with Link phone. */
data class SeatActions(
    val onClaim: (moduleId: Int, slot: Int, seatName: String) -> Unit = { _, _, _ -> },
    val onDismiss: () -> Unit = {},
)

/** The table's seats and Sigils, as the portal's Players tab shows them. */
@Composable
fun PlayersTab(
    summary: TableSummary,
    myPlayer: Int?,
    nowMs: Long,
    labelFor: (Int) -> String,
    endpoint: String,
    claim: SeatClaim? = null,
    seatActions: SeatActions = SeatActions(),
    people: @Composable () -> Unit = {},
) {
    val p = palette
    Column(verticalArrangement = Arrangement.spacedBy(14.dp)) {
        BrassCard {
            Eyebrow("Players")
            Text("Live table seats, turn state and physical Sigil identity.", color = p.muted, style = MaterialTheme.typography.bodySmall)
            claim?.let { ClaimNotice(it, seatActions.onDismiss) }
            if (summary.players.isEmpty()) {
                EmptyNote("No players have joined. Choose Join on a Sigil, or join from the Game tab.")
            }
            if (summary.settings.twoHeadedGiant) {
                Text(
                    "Two-Headed Giant: players 1 and 2 are a team, then 3 and 4. Teammates share one life total and one turn.",
                    color = p.muted,
                    style = MaterialTheme.typography.bodySmall,
                )
            }
            summary.players.forEachIndexed { index, player ->
                // Two-Headed Giant: a heading over each team (teammates are neighbours).
                if (player.team != null && player.team != summary.players.getOrNull(index - 1)?.team) {
                    Eyebrow("Team ${player.team}")
                }
                val mine = player.playerNumber == myPlayer
                // Only a physical seat can be confirmed with Link phone.
                val onUse = if (!mine && player.controller.kind == ControllerHandle.Kind.PHYSICAL && claim?.waiting != true) {
                    { seatActions.onClaim(player.controller.id, player.slot, sigilSeatName(player)) }
                } else {
                    null
                }
                RosterCard(player, summary, mine, nowMs, labelFor, onUse)
            }
            if (summary.players.any { it.controller.kind == ControllerHandle.Kind.PHYSICAL && it.playerNumber != myPlayer }) {
                Text(
                    "Use this seat links a Sigil seat to this phone: press Link phone on that Sigil to confirm. " +
                        "Without signing in first, it works only for a profile that has no PIN.",
                    color = p.faint,
                    style = MaterialTheme.typography.bodySmall,
                )
            }
        }
        people()
        BrassCard {
            Eyebrow("Sigils at this table")
            if (summary.physicalSigils.isEmpty()) {
                EmptyNote("No physical Sigils are seated.")
            }
            summary.physicalSigils.forEach { sigil ->
                Row(
                    Modifier
                        .fillMaxWidth()
                        .clip(RoundedCornerShape(12.dp))
                        .background(p.inset)
                        .border(1.dp, p.line, RoundedCornerShape(12.dp))
                        .padding(12.dp),
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(12.dp),
                ) {
                    Text("Sigil ${sigil.controller.id + 1}", color = p.text, style = MaterialTheme.typography.titleMedium, modifier = Modifier.weight(1f))
                    Column(horizontalAlignment = Alignment.End) {
                        sigil.seats.forEach { seat ->
                            Text("Seat ${seatLabel(seat.slot)} · ${seat.label}", color = p.muted, style = MaterialTheme.typography.bodySmall)
                        }
                    }
                }
            }
            Text(
                "Only Sigils seated at the current table are listed. Pair new Sigils from the Atlas screen.",
                color = p.faint,
                style = MaterialTheme.typography.bodySmall,
            )
        }
        BrassCard {
            Eyebrow("Invite players")
            Text(
                "Players join the table's Wi-Fi, then open the portal at $endpoint, or install the TurnHub app. " +
                    "The Atlas screen's QR button shows a code to scan.",
                color = p.muted,
                style = MaterialTheme.typography.bodyMedium,
            )
        }
    }
}

private fun sigilSeatName(player: TablePlayer) = "Sigil ${player.controller.id + 1} seat ${seatLabel(player.slot)}"

/** Where a Link phone claim stands; announced to screen readers as it changes. */
@Composable
private fun ClaimNotice(claim: SeatClaim, onDismiss: () -> Unit) {
    val p = palette
    Row(
        Modifier
            .fillMaxWidth()
            .clip(RoundedCornerShape(12.dp))
            .background(p.inset)
            .border(1.dp, if (claim.isError) p.bad else if (claim.waiting) p.active else p.good, RoundedCornerShape(12.dp))
            .padding(12.dp)
            .semantics { liveRegion = LiveRegionMode.Polite },
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(10.dp),
    ) {
        Text(
            claim.message,
            color = if (claim.isError) p.bad else p.text,
            style = MaterialTheme.typography.bodyMedium,
            modifier = Modifier.weight(1f),
        )
        if (!claim.waiting) ToneButton("OK", onDismiss)
    }
}

@Composable
internal fun EmptyNote(text: String) {
    val p = palette
    Text(
        text,
        color = p.muted,
        modifier = Modifier
            .fillMaxWidth()
            .clip(RoundedCornerShape(12.dp))
            .border(1.dp, p.lineStrong, RoundedCornerShape(12.dp))
            .padding(20.dp),
        style = MaterialTheme.typography.bodyMedium,
    )
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun RosterCard(
    player: TablePlayer,
    summary: TableSummary,
    mine: Boolean,
    nowMs: Long,
    labelFor: (Int) -> String,
    onUseSeat: (() -> Unit)? = null,
) {
    val p = palette
    val active = summary.hasTurn(player.playerNumber) &&
        (summary.state == TableState.RUNNING || summary.state == TableState.PAUSED)
    val winner = summary.isWinner(player.playerNumber)
    val border = when {
        winner -> p.accent
        active -> p.active
        else -> p.line
    }
    Column(
        Modifier
            .fillMaxWidth()
            .alpha(if (player.eliminated) .65f else 1f)
            .clip(RoundedCornerShape(16.dp))
            .background(p.surface2)
            .border(if (active || winner) 2.dp else 1.dp, border, RoundedCornerShape(16.dp))
            .padding(14.dp),
        verticalArrangement = Arrangement.spacedBy(10.dp),
    ) {
        // The seat's facts read as one item; the button stays its own target.
        Row(
            Modifier.semantics(mergeDescendants = true) { },
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            PlayerAvatar(player.label, player.playerNumber, player.avatar)
            Column(Modifier.weight(1f)) {
                Text(player.label + if (mine) " (you)" else "", color = p.text, style = MaterialTheme.typography.titleMedium)
                val controller = when (player.controller.kind) {
                    ControllerHandle.Kind.PHYSICAL -> "Sigil ${player.controller.id + 1} · seat ${seatLabel(player.slot)}"
                    ControllerHandle.Kind.VIRTUAL -> "Phone controller"
                    ControllerHandle.Kind.UNKNOWN -> "Controller ${player.controller.id}"
                }
                Text("${player.team?.let { "Team $it · " } ?: ""}Player ${player.playerNumber} · $controller", color = p.muted, style = MaterialTheme.typography.bodySmall)
            }
            player.life?.let {
                Column(horizontalAlignment = Alignment.End) {
                    Text(it.toString(), color = p.text, style = MaterialTheme.typography.headlineSmall)
                    Text("LIFE", color = p.faint, style = MaterialTheme.typography.labelSmall)
                }
            }
        }
        FlowRow(horizontalArrangement = Arrangement.spacedBy(6.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
            if (active) {
                StatusBadge(
                    if (summary.state == TableState.PAUSED) "Active · paused"
                    else "Active · ${TableClock.format(TableClock.turnElapsedMs(summary, nowMs))}",
                    Tone.ACTIVE,
                )
            }
            if (winner) StatusBadge("Winner", Tone.ACCENT)
            if (player.eliminated) StatusBadge("Eliminated", Tone.BAD)
            if (summary.starterPlayerNumber == player.playerNumber) StatusBadge("Starter", Tone.INFO)
            if (summary.pending.passPlayer == player.playerNumber) StatusBadge("Passing", Tone.WARN)
            if (summary.pending.winConfirmationPlayer == player.playerNumber) StatusBadge("Deciding win claim", Tone.WARN)
            if (player.turnsCompleted > 0) StatusBadge("${player.turnsCompleted} turns")
        }
        player.commanderDamage.filter { it.damage.any { d -> d > 0 } }.takeIf { it.isNotEmpty() }?.let { list ->
            Text(
                "Commander damage: " + list.joinToString { "${labelFor(it.sourcePlayer)} ${it.damage.joinToString("/")}" },
                color = p.muted,
                style = MaterialTheme.typography.bodySmall.copy(fontWeight = FontWeight.Medium),
            )
        }
        onUseSeat?.let { ToneButton("Use this seat", it, tone = Tone.INFO) }
    }
}
