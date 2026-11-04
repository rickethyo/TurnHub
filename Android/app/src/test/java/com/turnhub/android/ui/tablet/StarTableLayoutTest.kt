package com.turnhub.android.ui.tablet

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class StarTableLayoutTest {
    @Test fun `curved heading life facts and controls have separate fitted bands`() {
        for ((w, h) in listOf(800f to 1000f, 800f to 1280f, 1280f to 800f, 1000f to 1000f)) {
            for (card in starTableGeometry(w, h)) {
                val flow = starSeatFlow(card)
                val bands = listOf(flow.life, flow.facts, flow.heading, flow.controls)
                val frame = card.backdrop!!
                bands.forEachIndexed { i, band ->
                    assertTrue(band.width > 0 && band.height > 0)
                    assertTrue(band.left >= 0 && band.top >= 0)
                    assertTrue(band.left + band.width <= frame.width + .01f && band.top + band.height <= frame.height + .01f)
                    if (i > 0) assertTrue("text and actions overlap", band.top >= bands[i - 1].top + bands[i - 1].height - .01f)
                    val angle = Math.toRadians(frame.degrees.toDouble())
                    val c = kotlin.math.cos(angle).toFloat(); val s = kotlin.math.sin(angle).toFloat()
                    for (corner in listOf(StarPoint(band.left, band.top), StarPoint(band.left + band.width, band.top),
                            StarPoint(band.left + band.width, band.top + band.height), StarPoint(band.left, band.top + band.height))) {
                        val dx = corner.x - frame.width / 2; val dy = corner.y - frame.height / 2
                        val point = StarPoint(frame.x + c * dx - s * dy, frame.y + s * dx + c * dy)
                        val sides = card.seat.indices.map { j ->
                            val a = card.seat[j]; val b = card.seat[(j + 1) % card.seat.size]
                            (b.x - a.x) * (point.y - a.y) - (b.y - a.y) * (point.x - a.x)
                        }
                        assertTrue("band leaves its seat", sides.all { it >= -.1f } || sides.all { it <= .1f })
                    }
                }
            }
        }
    }
    private fun separated(a: List<StarPoint>, b: List<StarPoint>): Boolean = (a + b).indices.any { i ->
        val points = if (i < a.size) a else b
        val j = if (i < a.size) i else i - a.size
        val edge = StarPoint(points[(j + 1) % points.size].x - points[j].x, points[(j + 1) % points.size].y - points[j].y)
        fun projection(p: StarPoint) = -edge.y * p.x + edge.x * p.y
        val aa = a.map(::projection)
        val bb = b.map(::projection)
        aa.max() <= bb.min() + .01f || bb.max() <= aa.min() + .01f
    }

    @Test fun `all rotated cards remain inside screen without overlapping another seat or menu`() {
        for ((width, height) in listOf(320f to 480f, 600f to 600f, 800f to 1280f, 1280f to 800f,
                980f to 1500f, 1200f to 2000f, 1600f to 720f, 720f to 1600f)) {
            val cards = starTableGeometry(width, height)
            assertEquals(5, cards.size)
            cards.forEachIndexed { i, card ->
                assertTrue("$width x $height seat $i", card.width > 0 && card.height > 0)
                assertEquals(starSpot(i).degrees, card.degrees)
                card.corners().forEach { point ->
                    assertTrue("outside $width x $height: $point", point.x >= -.01f && point.x <= width + .01f &&
                        point.y >= -.01f && point.y <= height + .01f)
                }
                for (j in 0 until i) assertTrue("overlapping seats $i / $j", separated(card.corners(), cards[j].corners()))
                val hubRadius = minOf(42f, minOf(width, height) * .10f)
                // Test the actual circular menu rather than its square bounding box.
                val angle = Math.toRadians(card.degrees.toDouble())
                val outwardX = -kotlin.math.sin(angle).toFloat()
                val outwardY = kotlin.math.cos(angle).toFloat()
                assertTrue(card.corners().minOf { (it.x - width / 2) * outwardX + (it.y - height / 2) * outwardY } >= hubRadius)
                for (j in 0 until i) assertTrue("overlapping artwork $i / $j", separated(card.seat, cards[j].seat))
            }
        }
    }

    @Test fun `portrait cards gain usable area over fixed squares and fit their own seat`() {
        val width = 800f
        val height = 1280f
        val cards = starTableGeometry(width, height)
        val previousArea = 5 * (width * .34f) * (width * .34f)
        val fittedArea = cards.sumOf { (it.width * it.height).toDouble() }
        assertTrue("safe control area $fittedArea vs $previousArea", fittedArea > previousArea)
        fun area(points: List<StarPoint>) = kotlin.math.abs(points.indices.sumOf { i ->
            val a = points[i]; val b = points[(i + 1) % points.size]
            (a.x * b.y - a.y * b.x).toDouble()
        }) / 2
        assertTrue("artwork fills the table around the center menu", cards.sumOf { area(it.seat) } > width * height * .85)
        assertTrue(cards[0].width > width * .4f)
        assertTrue(cards[2].height > width * .34f)
    }
    @Test fun `seats run clockwise from the top, each facing its own edge`() {
        val top = starSpot(0)
        assertEquals(0f, top.x, 1e-4f)
        assertEquals(-1f, top.y, 1e-4f)
        assertEquals(-180, top.degrees)
        // Seat 2 is to the right of seat 1, as in the setup's star.
        assertEquals(true, starSpot(1).x > 0)
        assertEquals(listOf(-180, -108, -36, 36, 108), (0 until 5).map { starSpot(it).degrees })
    }
}
