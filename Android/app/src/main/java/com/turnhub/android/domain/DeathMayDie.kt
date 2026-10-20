package com.turnhub.android.domain

/**
 * Cthulhu: Death May Die's investigators, by the box they come in, for the
 * lobby's picker. Names only: each player still plays from their own
 * dashboard. Compiled from the community Death May Die wiki
 * (cthulhu-death-may-die.fandom.com, "Investigators") and checked against
 * Make Craft Game's character guides; a name missing here can be typed in.
 */
data class InvestigatorBox(val title: String, val investigators: List<String>)

object DeathMayDie {
    /** The dashboard's stress track holds 5 (owner, 2026-10-19). */
    const val MAX_STRESS = 5

    /** Five wounds is the most an investigator survives; the sixth kills them (owner, 2026-10-19). */
    const val DEATH_WOUNDS = 6

    val boxes: List<InvestigatorBox> = DmdContent.boxes

    /**
     * The catalogue's full reference image URL: box art for Season 1,
     * a community photograph or scan of the dashboard after.
     */
    private val art: Map<String, String> = DmdContent.investigatorArt

    /** Where to download [name]'s picture for their tile; null when the wiki has none. */
    fun artUrl(name: String): String? = art[name]

    /** A dashboard picture is wide: its portrait is the left-hand panel, cropped as left, top, right, bottom. */
    val dashboardPortrait = listOf(.15f, .13f, .44f, .80f)

    /** Every investigator once, in box order. */
    val all: List<String> get() = boxes.flatMap { it.investigators }

    /** The box [name] comes in; null for a name typed in. */
    fun boxOf(name: String): String? = boxes.firstOrNull { name in it.investigators }?.title

    /**
     * Who has taken their turn this round: [acted] (player indices) once each
     * living player has, the round is over and the next starts empty.
     */
    fun roundOver(acted: Set<Int>, living: List<Int>): Boolean = living.isNotEmpty() && living.all { it in acted }
}
