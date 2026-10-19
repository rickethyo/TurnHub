package com.turnhub.android.ui.tablet

import com.turnhub.android.protocol.DoomBag
import org.junit.Assert.*
import org.junit.Test

class BagPromptStateTest {
    private val preview = DoomBag(cycle = 1, warmup = 4)
    private val due = preview.copy(warmup = 0, pending = true, pendingTurn = 5)
    private val result = due.copy(cycle = 2, pending = false, pendingTurn = 0, lastCheck = 1)
    private val nextDue = result.copy(pending = true, pendingTurn = 5)

    @Test fun `closing a preview still opens the first due check`() {
        val prompt = BagPromptState().open()
        assertTrue(prompt.shouldShow(preview))
        val closed = prompt.dismiss(preview)
        assertFalse(closed.shouldShow(preview))
        assertTrue(closed.shouldShow(due))
    }

    @Test fun `closing a result still opens the next due check`() {
        val prompt = BagPromptState().dismiss(due)
        assertTrue(prompt.shouldShow(result))
        val closed = prompt.dismiss(result)
        assertFalse(closed.shouldShow(result))
        assertTrue(closed.shouldShow(nextDue))
    }

    @Test fun `reviewing life totals hides only the current pending check`() {
        val reviewed = BagPromptState().dismiss(due)
        assertFalse(reviewed.shouldShow(due))
        assertTrue(reviewed.shouldShow(result))
        assertTrue(reviewed.dismiss(result).shouldShow(nextDue))
    }

    @Test fun `manual reopen works after reviewing the pending check`() {
        val reviewed = BagPromptState().dismiss(due)
        assertTrue(reviewed.open().shouldShow(due))
        assertFalse(reviewed.open().dismiss(due).shouldShow(due))
    }

    @Test fun `closing later previews cannot hide the next check`() {
        val reviewed = BagPromptState().dismiss(due).dismiss(result)
        val previewed = reviewed.open().dismiss(result)
        assertTrue(previewed.shouldShow(nextDue))
        // A credited elimination advances the cycle without a bag result.
        assertTrue(previewed.shouldShow(nextDue.copy(cycle = 3)))
    }
}
