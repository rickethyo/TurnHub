package com.turnhub.android.ui.tablet

import com.turnhub.android.protocol.DoomBag

/** Local window review, separate from the authority's bag state. */
internal data class BagPromptState(
    val opened: Boolean = false,
    val reviewedPendingCycle: Long = 0,
    val dismissedResultCheck: Long = 0,
) {
    fun shouldShow(bag: DoomBag): Boolean = opened ||
        (bag.pending && reviewedPendingCycle != bag.cycle) || bag.lastCheck > dismissedResultCheck

    fun open() = copy(opened = true)

    fun dismiss(bag: DoomBag?) = copy(
        opened = false,
        // A preview or the previous result must not acknowledge a future check.
        reviewedPendingCycle = if (bag?.pending == true) bag.cycle else reviewedPendingCycle,
        dismissedResultCheck = bag?.lastCheck ?: dismissedResultCheck,
    )
}
