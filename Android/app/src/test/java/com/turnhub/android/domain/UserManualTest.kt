package com.turnhub.android.domain

import com.turnhub.android.domain.UserManual.Block
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import java.io.File

class UserManualTest {

    @Test
    fun `parses headings, paragraphs, bullets and numbered steps`() {
        val manual = UserManual.parse(
            """
            <!-- generated -->

            # TurnHub User Manual

            ## Prototype Edition v9

            # 1. Meet TurnHub

            **Prototype Notice**
            This may change.

            - First

            1. Tap **Menu**, then Pair.

            ### Game
            """.trimIndent(),
        )

        assertEquals("TurnHub User Manual", manual.title)
        assertEquals("Prototype Edition v9", manual.edition)
        assertEquals(
            listOf(
                Block.Heading(1, "1. Meet TurnHub"),
                Block.Paragraph(UserManual.Rich("Prototype Notice\nThis may change.", listOf(0..15))),
                Block.Bullet(UserManual.Rich("First")),
                Block.Step(1, UserManual.Rich("Tap Menu, then Pair.", listOf(4..7))),
                Block.Heading(3, "Game"),
            ),
            manual.blocks,
        )
        assertEquals(listOf(0 to "1. Meet TurnHub"), manual.chapters)
    }

    @Test
    fun `unmatched bold marker is kept as text`() {
        assertEquals(UserManual.Rich("a **b"), UserManual.rich("a **b"))
    }

    @Test
    fun `missing edition heading is tolerated`() {
        val manual = UserManual.parse("# Title\n\nBody")
        assertNull(manual.edition)
        assertEquals(listOf<Block>(Block.Paragraph(UserManual.Rich("Body"))), manual.blocks)
    }

    @Test
    fun `shipped asset parses into the numbered chapters`() {
        val asset = generateSequence(File(System.getProperty("user.dir")!!).absoluteFile) { it.parentFile }
            .map { File(it, "app/src/main/assets/manual.md") }
            .first { it.isFile }
        val manual = UserManual.parse(asset.readText())

        assertEquals("TurnHub User Manual", manual.title)
        assertTrue(manual.chapters.size >= 20)
        assertTrue(manual.chapters.first().second.startsWith("1. "))
        assertTrue(manual.blocks.none { block ->
            val text = when (block) {
                is Block.Heading -> block.text
                is Block.Paragraph -> block.text.text
                is Block.Bullet -> block.text.text
                is Block.Step -> block.text.text
            }
            "**" in text
        })
    }
}
