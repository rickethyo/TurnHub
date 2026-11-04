package com.turnhub.android.standalone

import com.turnhub.android.domain.DeathMayDie
import com.turnhub.android.domain.DeathMayDieStory
import com.turnhub.android.domain.ElderOneTrack
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/** Checks on Death May Die's local-only catalogue (src/dmd); never committed. */
class DmdContentTest {
    @Test fun `the roster lists every box once with no repeated names`() {
        assertEquals(DeathMayDie.all.size, DeathMayDie.all.distinct().size)
        assertEquals("Season 1 (core box)", DeathMayDie.boxOf("Rasputin"))
        assertTrue(DeathMayDie.boxes.all { it.investigators.isNotEmpty() })
        assertTrue(DeathMayDie.all.all { it.length <= StandaloneGame.MAX_NAME })
        // Pictures only for investigators on the roster, from the wiki's image host.
        assertTrue(DeathMayDie.artUrl("Rasputin")!!.startsWith("https://static.wikia.nocookie.net/cthulhu-death-may-die/images/"))
        assertNull(DeathMayDie.artUrl("Nikola Tesla"))
    }

    @Test fun `the Elder One and episode lists name each once`() {
        val elders = DeathMayDieStory.elderOnes.flatMap { it.names }
        assertEquals(elders.size, elders.distinct().size)
        assertTrue("Cthulhu" in elders && "Hastur" in elders)
        val episodes = DeathMayDieStory.episodes.flatMap { it.names }
        assertEquals(episodes.size, episodes.distinct().size)
        assertTrue((elders + episodes).all { it.length <= ElderOneTrack.MAX_NAME })
    }
}
