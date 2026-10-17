package com.turnhub.android.domain

import com.turnhub.android.protocol.AtlasBattery
import com.turnhub.android.protocol.AvatarIcon
import com.turnhub.android.protocol.CommanderDamage
import com.turnhub.android.protocol.LifeRequest
import com.turnhub.android.protocol.Nudge
import com.turnhub.android.protocol.PendingDecisions
import com.turnhub.android.protocol.PlayerCounters
import com.turnhub.android.protocol.TableSettings
import com.turnhub.android.protocol.TableState
import com.turnhub.android.protocol.TurnTimer

/**
 * A UI-oriented summary of one authoritative state snapshot
 * (protocol/state-v0.1.schema.json), combined with the identity fields Atlas
 * reports from `GET /api/v1/info` (protocol/http-v1.md).
 *
 * This lives in `domain/`, not `protocol/`, because no single Atlas response
 * looks like this. It is always rebuilt whole from the latest snapshot
 * ([TableSummaryMapper]); nothing in it is advanced locally, so it is a cache
 * of Atlas state for rendering, never a second source of truth.
 */
data class TableSummary(
    val atlasId: String,
    val bootId: String,
    val firmwareVersion: String,
    /** Unsigned 32-bit revision of the snapshot this summary was built from. */
    val revision: Long,
    val state: TableState,
    val settings: TableSettings,
    val host: ControllerHandle?,
    val starterPlayerNumber: Int?,
    val activePlayerNumber: Int?,
    val winnerPlayerNumber: Int?,
    val pending: PendingDecisions,
    /** Atlas-sampled clocks; may change while [revision] stays the same. */
    val gameElapsedMs: Long,
    val turnElapsedMs: Long,
    /** Atlas's turn-timer phase and countdown sample. */
    val turnTimer: TurnTimer,
    /**
     * Local monotonic time (ms) when this snapshot arrived. Lets the UI render
     * clocks between polls as `sampled + (now - receivedAtMs)`; the next
     * snapshot always replaces that estimate (protocol/README.md allows
     * locally rendered time-sensitive display between snapshots).
     */
    val receivedAtMs: Long,
    /** Atlas's uptime clock when it sampled this snapshot (unsigned 32-bit, wraps). */
    val sampledAtMs: Long = 0,
    val players: List<TablePlayer>,
    /** Physical Sigils currently represented at the table, derived from [players]. */
    val physicalSigils: List<PhysicalSigilAtTable>,
    /** The last nudge since Atlas booted; its age is as of [sampledAtMs]. */
    val nudge: Nudge? = null,
    /** Atlas's own battery, for the header gauge; null with no cell. */
    val battery: AtlasBattery? = null,
    val monarchPlayerNumber: Int? = null,
    val initiativePlayerNumber: Int? = null,
    val game: Int = 1,
    val games: List<com.turnhub.android.protocol.VenueGame> = emptyList(),
    /**
     * Every winner; [winnerPlayerNumber] is the first. Star Commander allies whose
     * enemies are out at once share the victory (a shared victory is never a draw).
     */
    val winnerPlayerNumbers: List<Int> = listOfNotNull(winnerPlayerNumber),
    /** Atlas: the finished game lasted under a minute and isn't in stats until the table counts it. */
    val shortGameUncounted: Boolean = false,
    val localTimerRules: LocalTimerRules? = null,
    val turnExtensionMs: Long = 0,
    /** Pokémon (phone-hosted games): what the last Pokémon Checkup did, until the table dismisses it. */
    val pokemonCheckup: List<String> = emptyList(),
    /** Cooperative games (Death May Die, phone-hosted): the round being played; null otherwise. */
    val round: Int? = null,
    /** Phone engine's eligible recipients after the active investigator finishes this turn. */
    val passPlayerNumbers: List<Int> = emptyList(),
    /** Phone-hosted Death May Die optional setup rule. */
    val useRelics: Boolean = false,
) {
    /**
     * A finished match with no winner: the table ended it as a draw by holding
     * End match on the Atlas touchscreen (protocol/http-v1.md). Derived, never stored.
     */
    val endedInDraw: Boolean
        get() = state == TableState.GAME_OVER && winnerPlayerNumber == null && winnerPlayerNumbers.isEmpty()

    private fun teamOf(playerNumber: Int?): Int? = players.firstOrNull { it.playerNumber == playerNumber }?.team

    /** The same player, or Two-Headed Giant teammates (Atlas reports each player's team). */
    fun sameTeam(a: Int?, b: Int?): Boolean =
        a != null && b != null && (a == b || (teamOf(a) != null && teamOf(a) == teamOf(b)))

    /** The active player, or (Two-Headed Giant) their teammate: the turn is the team's. */
    fun hasTurn(playerNumber: Int): Boolean = sameTeam(activePlayerNumber, playerNumber)

    /** "Team N" for a Two-Headed Giant player, else null (name the player). */
    fun teamLabel(playerNumber: Int?): String? = teamOf(playerNumber)?.let { "Team $it" }

    /** A winner (every one of a shared victory), or (Two-Headed Giant) the winner's teammate. */
    fun isWinner(playerNumber: Int): Boolean =
        (winnerPlayerNumbers.ifEmpty { listOfNotNull(winnerPlayerNumber) }).any { sameTeam(it, playerNumber) }

    // --- Star Commander ([StarCommander]): relationships by seat, player N is seat N-1 ----

    /** A five-player Star Commander table, whose relationships the screens can draw. */
    val star: Boolean get() = settings.profile.starCommander && players.size == StarCommander.PLAYERS

    private fun seated(seats: List<Int>): List<TablePlayer> = seats.mapNotNull { s -> players.firstOrNull { it.playerNumber == s + 1 } }

    fun starAllies(playerNumber: Int): List<TablePlayer> = if (!star) emptyList() else seated(StarCommander.allies(playerNumber - 1))

    fun starEnemies(playerNumber: Int): List<TablePlayer> = if (!star) emptyList() else seated(StarCommander.enemies(playerNumber - 1))

    /** Whether [source] may deal [receiver] Commander damage: never between Star Commander allies. */
    fun commanderDamageAllowed(receiver: Int, source: Int): Boolean =
        receiver != source && !(star && StarCommander.allied(receiver - 1, source - 1)) &&
            // Commander damage is combat damage, and a Two-Headed Giant team never attacks itself (CR 805.10).
            !(settings.twoHeadedGiant && (receiver - 1) / 2 == (source - 1) / 2)
}

/** A controller seat: handle + slot. How `/api/seats` names are matched to players. */
data class SeatKey(val moduleId: Int, val slot: Int)

/** One participant as the Home screen renders it. */
data class TablePlayer(
    val playerNumber: Int,
    /** Atlas-provided name (state, else `/api/seats`), or a neutral "Player N". */
    val label: String,
    /** True when [label] came from Atlas rather than the "Player N" fallback. */
    val hasName: Boolean,
    val controller: ControllerHandle,
    val slot: Int,
    /** Two-Headed Giant team (players 1+2 are team 1); null outside Two-Headed Giant. */
    val team: Int? = null,
    val participantId: Long,
    /** The seated profile; tablet mode removes a player by it. */
    val profileId: String? = null,
    val eliminated: Boolean,
    val conceded: Boolean = false,
    val counters: PlayerCounters = PlayerCounters(),
    /** Null in the lobby. */
    val life: Int?,
    val turnsCompleted: Long,
    val commanderDamage: List<CommanderDamage>,
    val lifeRequest: LifeRequest?,
    /** The seat's preset avatar, when Atlas has one for it. */
    val avatar: AvatarIcon? = null,
    /** Disposable local presentation; never a game-state field. */
    val color: String? = null,
    val tileBackground: TileBackground = TileBackground(),
    val demerits: Int = 0,
    val extensionsAvailable: Int = 0,
    /**
     * The card whose art fills this player's tile (phone-hosted Magic games only): their
     * commander, unless [cardOnly]. Name it with [namedCommander].
     */
    val commander: String? = null,
    /** The Scryfall printing whose art the card shows; null for Scryfall's default. */
    val commanderArt: String? = null,
    /** [commander] is only the tile's art (a non-Commander format, or a random card), never named as a commander. */
    val cardOnly: Boolean = false,
    /** Pokémon and Lorcana (phone-hosted games): this turn's actions; Pokémon's Special Conditions. */
    val turn: TurnStatus = TurnStatus(),
    /** Death May Die (phone-hosted games): the investigator this player plays. */
    val investigator: String? = null,
    /** Death May Die: stress tokens (wounds are [life]). */
    val stress: Int = 0,
    val dashboard: InvestigatorDashboard = InvestigatorDashboard(),
    /** Cooperative games: this player has taken their turn this round. */
    val actedThisRound: Boolean = false,
) {
    /** The commander this player named, for labels and menus; null when the card is only tile art. */
    val namedCommander: String? get() = commander.takeUnless { cardOnly }
}

/**
 * A physical Sigil that is seated at the current table, and which seats it
 * holds. Built only from `players[]`: it says nothing about pairing,
 * connectivity or Sigils that are paired but not seated.
 */
data class PhysicalSigilAtTable(
    val controller: ControllerHandle,
    val seats: List<Seat>,
) {
    data class Seat(val slot: Int, val playerNumber: Int, val label: String)
}

/**
 * Atlas's controller handle (`moduleId` on the wire). Physical Sigil handles are
 * 0-7 and virtual/browser handles 8-23 (protocol/http-v1.md). A handle is not a
 * durable device ID.
 */
@JvmInline
value class ControllerHandle(val id: Int) {
    val kind: Kind
        get() = when (id) {
            in PHYSICAL -> Kind.PHYSICAL
            in VIRTUAL -> Kind.VIRTUAL
            else -> Kind.UNKNOWN
        }

    enum class Kind { PHYSICAL, VIRTUAL, UNKNOWN }

    companion object {
        val PHYSICAL = 0..7
        val VIRTUAL = 8..23
    }
}

/** Seat letter used by Atlas and the portal: slot 1 = A, slot 2 = B. */
fun seatLabel(slot: Int): String = when (slot) {
    1 -> "A"
    2 -> "B"
    else -> "slot $slot"
}
