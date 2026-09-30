package com.turnhub.android.domain

/**
 * The user manual shipped inside the app (assets/manual.md), so it reads
 * offline and before any Atlas is reachable. The asset is generated from the
 * newest "Documentation/User Manual" .docx by Android/tools/export_manual.py;
 * this parser reads only the Markdown subset that script writes.
 */
data class UserManual(val title: String, val edition: String?, val blocks: List<Block>) {

    /** A run of text with the character ranges shown in bold. */
    data class Rich(val text: String, val bold: List<IntRange> = emptyList())

    sealed interface Block {
        data class Heading(val level: Int, val text: String) : Block
        data class Paragraph(val text: Rich) : Block
        data class Bullet(val text: Rich) : Block
        data class Step(val number: Int, val text: Rich) : Block
    }

    /** Top-level chapters ("1. Meet TurnHub" ...) and their index in [blocks]. */
    val chapters: List<Pair<Int, String>>
        get() = blocks.mapIndexedNotNull { index, block ->
            (block as? Block.Heading)?.takeIf { it.level == 1 }?.let { index to it.text }
        }

    companion object {
        private val STEP = Regex("""^(\d+)\.\s+(.*)$""", RegexOption.DOT_MATCHES_ALL)
        private val HEADING = Regex("""^(#{1,3})\s+(.*)$""")
        private val COMMENT = Regex("""<!--.*?-->""", RegexOption.DOT_MATCHES_ALL)

        fun parse(markdown: String): UserManual {
            val chunks = markdown.replace("\r\n", "\n")
                .replace(COMMENT, "")
                .split(Regex("""\n\s*\n"""))
                .map { it.trim() }
                .filter { it.isNotEmpty() }
            val blocks = chunks.map { chunk ->
                HEADING.matchEntire(chunk)?.let { m ->
                    return@map Block.Heading(m.groupValues[1].length, m.groupValues[2].trim())
                }
                STEP.matchEntire(chunk)?.let { m ->
                    return@map Block.Step(m.groupValues[1].toInt(), rich(m.groupValues[2]))
                }
                if (chunk.startsWith("- ")) Block.Bullet(rich(chunk.removePrefix("- ")))
                else Block.Paragraph(rich(chunk))
            }.toMutableList()
            // The document opens with its title and edition; the screen shows
            // those in its own header rather than as body headings.
            val title = (blocks.firstOrNull() as? Block.Heading)?.takeIf { it.level == 1 }
                ?.also { blocks.removeAt(0) }?.text ?: "TurnHub User Manual"
            val edition = (blocks.firstOrNull() as? Block.Heading)?.takeIf { it.level == 2 }
                ?.also { blocks.removeAt(0) }?.text
            return UserManual(title, edition, blocks)
        }

        /** Strips `**bold**` markers, remembering where they were. */
        internal fun rich(source: String): Rich {
            val out = StringBuilder()
            val bold = mutableListOf<IntRange>()
            var i = 0
            while (i < source.length) {
                val open = source.indexOf("**", i)
                val close = if (open >= 0) source.indexOf("**", open + 2) else -1
                if (open < 0 || close < 0) {
                    out.append(source, i, source.length)
                    break
                }
                out.append(source, i, open)
                val start = out.length
                out.append(source, open + 2, close)
                if (out.length > start) bold += start until out.length
                i = close + 2
            }
            return Rich(out.toString(), bold)
        }
    }
}
