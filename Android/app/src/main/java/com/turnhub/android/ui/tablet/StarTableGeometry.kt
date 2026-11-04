package com.turnhub.android.ui.tablet

import kotlin.math.abs
import kotlin.math.cos
import kotlin.math.min
import kotlin.math.sin
import kotlin.math.sqrt
import kotlin.math.tan

internal data class StarPoint(val x: Float, val y: Float)
internal data class StarBand(val left: Float, val top: Float, val width: Float, val height: Float)
internal data class StarSeatFlow(val life: StarBand, val facts: StarBand, val heading: StarBand, val controls: StarBand)

/** Each text/control band uses the width of the wedge at its own depth. */
internal fun starSeatFlow(card: StarCardRect, fontScale: Float = 1f): StarSeatFlow {
    val frame = requireNotNull(card.backdrop)
    val angle = Math.toRadians(frame.degrees.toDouble())
    val c = cos(angle).toFloat(); val s = sin(angle).toFloat()
    val polygon = card.seat.map {
        StarPoint((it.x - frame.x) * c + (it.y - frame.y) * s + frame.width / 2,
            -(it.x - frame.x) * s + (it.y - frame.y) * c + frame.height / 2)
    }
    fun band(top: Float, height: Float): StarBand? {
        val bottom = top + height
        var left = Float.NEGATIVE_INFINITY; var right = Float.POSITIVE_INFINITY
        val levels = listOf(top + .02f, bottom - .02f) + polygon.filter { it.y > top && it.y < bottom }.map { it.y }
        for (y in levels) {
            val xs = buildList {
                for (i in polygon.indices) {
                    val a = polygon[i]; val b = polygon[(i + 1) % polygon.size]
                    if (abs(a.y - b.y) < .001f) {
                        if (abs(y - a.y) < .001f) { add(a.x); add(b.x) }
                    } else if (y >= minOf(a.y, b.y) && y <= maxOf(a.y, b.y)) {
                        add(a.x + (y - a.y) / (b.y - a.y) * (b.x - a.x))
                    }
                }
            }
            if (xs.isEmpty()) return null
            left = maxOf(left, xs.min()); right = minOf(right, xs.max())
        }
        return if (right - left > 16) StarBand(left + 6, top, right - left - 12, height) else null
    }
    val headingHeight = minOf(100f * fontScale, frame.height * .27f)
    val factsHeight = minOf(58f * fontScale, frame.height * .16f)
    val controlsHeight = minOf(64f * fontScale, frame.height * .18f)
    var best: StarSeatFlow? = null
    var score = 0f
    for (lifeStep in 0..20) {
        val lifeHeight = 60f + lifeStep * 6f
        val total = lifeHeight + factsHeight + headingHeight + controlsHeight + 24
        if (total >= frame.height) continue
        for (step in 0..48) {
            val top = 4 + (frame.height - total - 8) * step / 48
            val life = band(top, lifeHeight) ?: continue
            val facts = band(top + lifeHeight + 8, factsHeight) ?: continue
            val heading = band(top + lifeHeight + factsHeight + 16, headingHeight) ?: continue
            val controls = band(top + lifeHeight + factsHeight + headingHeight + 24, controlsHeight) ?: continue
            if (life.width < 90 || facts.width < 90 || heading.width < 110 || controls.width < 158) continue
            val quality = life.width * life.height * 2 + heading.width * heading.height + facts.width * facts.height
            if (quality > score) { score = quality; best = StarSeatFlow(life, facts, heading, controls) }
        }
    }
    return best ?: run {
        val x = (card.x - frame.x) * c + (card.y - frame.y) * s + frame.width / 2 - card.width / 2
        val y = -(card.x - frame.x) * s + (card.y - frame.y) * c + frame.height / 2 - card.height / 2
        val h = card.height / 4
        StarSeatFlow(StarBand(x, y, card.width, h), StarBand(x, y + h, card.width, h),
            StarBand(x, y + h * 2, card.width, h), StarBand(x, y + h * 3, card.width, h))
    }
}

internal data class StarCardRect(val x: Float, val y: Float, val width: Float, val height: Float, val degrees: Int,
    val seat: List<StarPoint> = emptyList(), val backdrop: StarCardRect? = null) {
    fun corners(): List<StarPoint> {
        val angle = Math.toRadians(degrees.toDouble())
        val c = cos(angle).toFloat()
        val s = sin(angle).toFloat()
        return listOf(-1 to -1, 1 to -1, 1 to 1, -1 to 1).map { (u, v) ->
            StarPoint(x + c * width / 2 * u - s * height / 2 * v,
                y + s * width / 2 * u + c * height / 2 * v)
        }
    }
}

private data class StarEdge(val a: Float, val b: Float, val limit: Float)

/** Clip a feasible rectangle-center polygon against one inset seat boundary. */
private fun clipSeat(points: List<StarPoint>, edge: StarEdge, halfWidth: Float, halfHeight: Float): List<StarPoint> {
    if (points.isEmpty()) return points
    val limit = edge.limit - abs(edge.a) * halfWidth - abs(edge.b) * halfHeight
    fun distance(p: StarPoint) = edge.a * p.x + edge.b * p.y - limit
    return buildList {
        var previous = points.last()
        var before = distance(previous)
        for (point in points) {
            val after = distance(point)
            if ((before <= 0) != (after <= 0)) {
                val t = before / (before - after)
                add(StarPoint(previous.x + t * (point.x - previous.x), previous.y + t * (point.y - previous.y)))
            }
            if (after <= 0) add(point)
            previous = point
            before = after
        }
    }
}

/**
 * Fit each seat's largest readable rectangle into its own wedge of the screen.
 * Boundary insets account for the entire rotated footprint, so neither cards
 * nor their hit targets cross an adjacent seat or the central menu.
 */
internal fun starTableGeometry(width: Float, height: Float): List<StarCardRect> {
    require(width > 0 && height > 0)
    val short = min(width, height)
    val margin = min(8f, short * .02f)
    val gap = min(7f, short * .015f)
    val hub = min(52f, short * .13f)
    val slope = tan(Math.toRadians(36.0)).toFloat()
    val sectorInset = gap * sqrt(1 + slope * slope)
    val extent = width + height
    val initial = listOf(StarPoint(-extent, -extent), StarPoint(extent, -extent),
        StarPoint(extent, extent), StarPoint(-extent, extent))
    return (0 until 5).map { index ->
        val degrees = starSpot(index).degrees
        val angle = Math.toRadians(degrees.toDouble())
        val c = cos(angle).toFloat()
        val s = sin(angle).toFloat()
        // Work in the card's local axes: x tangent to its seat, y toward its player.
        val edges = listOf(
            StarEdge(c, -s, width / 2 - margin), StarEdge(-c, s, width / 2 - margin),
            StarEdge(s, c, height / 2 - margin), StarEdge(-s, -c, height / 2 - margin),
            StarEdge(1f, -slope, -sectorInset), StarEdge(-1f, -slope, -sectorInset),
            StarEdge(0f, -1f, -hub),
        )
        fun centers(w: Float, h: Float): List<StarPoint> {
            var polygon = initial
            for (edge in edges) polygon = clipSeat(polygon, edge, w / 2, h / 2)
            return polygon
        }
        var bestWidth = 0f
        var bestHeight = 0f
        var bestCenter = StarPoint(0f, hub)
        // Bound aspect ratios to keep the shared player card usable, then maximize its area.
        for (step in 0..36) {
            val ratio = .75f + step * .025f
            var low = 0f
            var high = maxOf(width, height)
            repeat(18) {
                val h = (low + high) / 2
                if (centers(h * ratio, h).isNotEmpty()) low = h else high = h
            }
            val w = low * ratio
            if (w * low > bestWidth * bestHeight) {
                val polygon = centers(w, low)
                bestCenter = StarPoint(polygon.map { it.x }.average().toFloat(), polygon.map { it.y }.average().toFloat())
                bestWidth = w
                bestHeight = low
            }
        }
        val sector = centers(0f, 0f)
        fun world(point: StarPoint) = StarPoint(width / 2 + c * point.x - s * point.y,
            height / 2 + s * point.x + c * point.y)
        val minX = sector.minOf { it.x }; val maxX = sector.maxOf { it.x }
        val minY = sector.minOf { it.y }; val maxY = sector.maxOf { it.y }
        val backdropCenter = world(StarPoint((minX + maxX) / 2, (minY + maxY) / 2))
        val center = world(bestCenter)
        StarCardRect(center.x, center.y, bestWidth, bestHeight, degrees, sector.map(::world),
            StarCardRect(backdropCenter.x, backdropCenter.y, maxX - minX, maxY - minY, degrees))
    }
}
