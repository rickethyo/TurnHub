package com.turnhub.android.standalone

import org.json.JSONArray
import org.json.JSONObject
import kotlin.random.Random

/**
 * Planechase (Magic Comprehensive Rules 901, 311, 312, 701.31; text of
 * 2026-09-25). TurnHub keeps the planar deck, the planar die and its cost, and
 * counters on the face-up plane; players apply what the cards say. Behavior is
 * in Documentation/engineering/GAMEPLAY.md "Planechase", and the rules are also
 * written as shared scenarios in protocol/scenarios/planechase/.
 *
 * Immutable like [StandaloneGame]: every change returns the next state. It
 * never draws randomness itself: the caller shuffles the deck and rolls the
 * die ([PlanarFace.roll]), so scenarios can script both.
 */
data class Planechase(
    /** Planechase is on for this table (Magic formats only). */
    val enabled: Boolean = false,
    val source: PlanarDeckSource = PlanarDeckSource.DIGITAL,
    /** Phenomena join the digital deck (at most two per player, CR 901.15a). */
    val phenomena: Boolean = true,
    val selection: PlanarSelection = PlanarSelection(),
    /** The single communal planar deck (CR 901.15), top card first; face down. */
    val deck: List<PlanarCard> = emptyList(),
    /** Face-up planes or one phenomenon; empty with physical cards. */
    val faceUp: List<PlanarCard> = emptyList(),
    /** Counters on each face-up card by id ([PHYSICAL_PLANE] for physical cards). */
    val counters: Map<String, Int> = emptyMap(),
    /** Planar die rolls each player has paid for this turn, by player index (CR 901.9, 901.12d). */
    val rolls: Map<Int, Int> = emptyMap(),
    val lastRoll: PlanarRoll? = null,
    /** How many times the table has planeswalked: with physical cards, the cue to turn over the next one. */
    val planeswalks: Int = 0,
    /** How many times chaos has ensued, from the die or a card: the cue to highlight the chaos ability. */
    val chaos: Int = 0,
) {
    /**
     * The table turns over its own cards: chosen in setup, or a digital game that
     * started without downloaded planes. Then TurnHub keeps no deck.
     */
    val physical: Boolean get() = source == PlanarDeckSource.PHYSICAL || (deck.isEmpty() && faceUp.isEmpty())

    /** A face-up phenomenon waits for the table to resolve it, then the planar controller planeswalks away (CR 704.6f). */
    val phenomenonPending: Boolean get() = faceUp.any { it.phenomenon }

    /** What [player]'s next roll costs this turn: {0}, then one more for each roll they paid for (CR 901.9). */
    fun cost(player: Int): Int = rolls[player] ?: 0

    /**
     * The game starts with [cards] as the deck, already shuffled (CR 901.5):
     * the top card is turned over until a plane comes up, phenomena go to the
     * bottom, and nothing triggers. Physical cards keep no deck.
     */
    fun dealt(cards: List<PlanarCard>): Planechase {
        val fresh = Planechase(enabled = enabled, source = source, phenomena = phenomena, selection = selection)
        if (source == PlanarDeckSource.PHYSICAL) return fresh
        var deck = cards.distinctBy { it.id }
        if (deck.none { !it.phenomenon }) return fresh.copy(deck = deck)
        while (deck.first().phenomenon) deck = deck.drop(1) + deck.first()
        return fresh.copy(deck = deck.drop(1), faceUp = listOf(deck.first()))
    }

    /**
     * TurnHub's deck arrived after a digital game started without it (the table
     * was using its own cards): [cards] are dealt as at the start of the game,
     * and this turn's roll costs and the history carry over.
     */
    fun dealtLate(cards: List<PlanarCard>): Planechase {
        if (source != PlanarDeckSource.DIGITAL || deck.isNotEmpty() || faceUp.isNotEmpty()) return this
        val dealt = dealt(cards)
        if (dealt.faceUp.isEmpty()) return this
        return dealt.copy(rolls = rolls, lastRoll = lastRoll, planeswalks = planeswalks, chaos = chaos)
    }

    /**
     * Planeswalk (CR 701.31b): every face-up card goes to the bottom of the deck,
     * then the top card turns face up. Counters on the old cards go with them.
     */
    fun planeswalk(): Planechase {
        val pile = deck + faceUp
        return copy(
            deck = pile.drop(1),
            faceUp = pile.take(1),
            counters = emptyMap(),
            planeswalks = planeswalks + 1,
        )
    }

    /**
     * [player] rolls the planar die and gets [face]. A roll from a card's effect
     * ([fromEffect]) costs nothing and doesn't raise the next roll's cost.
     */
    fun roll(player: Int, face: PlanarFace, fromEffect: Boolean = false): Planechase {
        val rolled = copy(
            rolls = if (fromEffect) rolls else rolls + (player to cost(player) + 1),
            lastRoll = PlanarRoll(player, face, if (fromEffect) 0 else cost(player), fromEffect, (lastRoll?.number ?: 0) + 1),
        )
        return when (face) {
            PlanarFace.BLANK -> rolled
            PlanarFace.CHAOS -> rolled.copy(chaos = chaos + 1)
            PlanarFace.PLANESWALK -> rolled.planeswalk()
        }
    }

    /** A card says chaos ensues (CR 311.7), without a roll. */
    fun chaosEnsues(): Planechase = copy(chaos = chaos + 1)

    /** Puts the top card of the deck on the bottom, for cards that say so. */
    fun topToBottom(): Planechase = if (deck.size < 2) this else copy(deck = deck.drop(1) + deck.first())

    /** Changes the counters on a face-up card; never below zero. */
    fun changeCounter(card: String, delta: Int): Planechase {
        val known = if (physical) card == PHYSICAL_PLANE else faceUp.any { it.id == card }
        val next = ((counters[card] ?: 0) + delta).coerceIn(0, MAX_COUNTERS)
        if (!known || delta == 0 || next == (counters[card] ?: 0)) return this
        return copy(counters = if (next == 0) counters - card else counters + (card to next))
    }

    /** The turn passed: every player's roll cost starts again at {0}. */
    fun newTurn(): Planechase = if (rolls.isEmpty()) this else copy(rolls = emptyMap())

    /** Back to the lobby: the table keeps its choices, the cards go back in the box. */
    fun cleared(): Planechase = Planechase(enabled = enabled, source = source, phenomena = phenomena, selection = selection)

    /** Saved with the local game, so a closed app resumes the same deck. */
    fun toJson(): JSONObject = JSONObject().apply {
        put("selection", selection.toJson())
        put("enabled", enabled)
        put("source", source.wireValue)
        put("phenomena", phenomena)
        put("deck", cardsToJson(deck))
        put("faceUp", cardsToJson(faceUp))
        put("counters", JSONObject().apply { counters.forEach { (card, n) -> put(card, n) } })
        put("rolls", JSONObject().apply { rolls.forEach { (player, n) -> put("$player", n) } })
        put("lastRoll", lastRoll?.let { r ->
            JSONObject().put("player", r.player).put("face", r.face.wireValue).put("cost", r.cost)
                .put("fromEffect", r.fromEffect).put("number", r.number)
        } ?: JSONObject.NULL)
        put("planeswalks", planeswalks)
        put("chaos", chaos)
    }

    companion object {
        /** Counter key for the plane on the table when the players use their own cards. */
        const val PHYSICAL_PLANE = "physical"
        const val MAX_COUNTERS = 999

        fun fromJson(json: JSONObject): Planechase {
            val counters = json.getJSONObject("counters")
            val rolls = json.getJSONObject("rolls")
            return Planechase(
                selection = json.optJSONObject("selection")?.let(PlanarSelection::fromJson) ?: PlanarSelection(),
                enabled = json.getBoolean("enabled"),
                source = PlanarDeckSource.fromWire(json.getString("source")) ?: error("Unsupported planar deck source"),
                phenomena = json.getBoolean("phenomena"),
                deck = cardsFromJson(json.getJSONArray("deck")),
                faceUp = cardsFromJson(json.getJSONArray("faceUp")),
                counters = counters.keys().asSequence().associateWith { counters.getInt(it) },
                rolls = rolls.keys().asSequence().associate { it.toInt() to rolls.getInt(it) },
                lastRoll = json.optJSONObject("lastRoll")?.let { r ->
                    PlanarRoll(r.getInt("player"), PlanarFace.fromWire(r.getString("face")) ?: error("Unsupported planar die face"),
                        r.getInt("cost"), r.getBoolean("fromEffect"), r.getInt("number"))
                },
                planeswalks = json.getInt("planeswalks"),
                chaos = json.getInt("chaos"),
            )
        }

        private fun cardsToJson(cards: List<PlanarCard>) = JSONArray().apply {
            cards.forEach { put(JSONObject().put("id", it.id).put("phenomenon", it.phenomenon)) }
        }

        private fun cardsFromJson(array: JSONArray): List<PlanarCard> = (0 until array.length()).map { i ->
            array.getJSONObject(i).let { PlanarCard(it.getString("id"), it.getBoolean("phenomenon")) }
        }

        /**
         * A shuffled shared deck from [pool] for [players] players: every plane, and
         * (when [phenomena]) up to two phenomena per player (CR 901.15a).
         */
        fun shuffledDeck(pool: List<PlanarCard>, players: Int, phenomena: Boolean, random: Random): List<PlanarCard> {
            val unique = pool.distinctBy { it.id }
            val planes = unique.filter { !it.phenomenon }
            val extra = if (phenomena) unique.filter { it.phenomenon }.shuffled(random).take(2 * players) else emptyList()
            return (planes + extra).shuffled(random)
        }
    }
}

/** One card in the planar deck: its Scryfall card id, and whether it's a phenomenon. */
data class PlanarCard(val id: String, val phenomenon: Boolean = false)

enum class PlanarDeckSource(val wireValue: String) {
    /** TurnHub holds and shuffles the cards (downloaded from Scryfall). */
    DIGITAL("digital"),
    /** The players turn over their own cards; TurnHub runs the die, the cost and counters. */
    PHYSICAL("physical"),
    ;

    companion object {
        fun fromWire(value: String): PlanarDeckSource? = entries.firstOrNull { it.wireValue == value }
    }
}

/** The planar die's faces (CR 901.3a): one Planeswalker symbol, one chaos symbol, four blank. */
enum class PlanarFace(val wireValue: String, val label: String) {
    BLANK("blank", "Blank"),
    CHAOS("chaos", "Chaos"),
    PLANESWALK("planeswalk", "Planeswalk"),
    ;

    companion object {
        fun fromWire(value: String): PlanarFace? = entries.firstOrNull { it.wireValue == value }

        /** A fair roll: one face in six is Planeswalk, one is Chaos. */
        fun roll(random: Random): PlanarFace = when (random.nextInt(6)) {
            0 -> PLANESWALK
            1 -> CHAOS
            else -> BLANK
        }
    }
}

/** The last roll: who rolled, the face, the mana it cost, and a running number so each roll shows once. */
data class PlanarRoll(val player: Int, val face: PlanarFace, val cost: Int, val fromEffect: Boolean, val number: Int)
