package com.turnhub.android.standalone

import com.turnhub.android.protocol.DoomBag
import org.json.JSONObject

/** Pure transitions called only by the standalone game's authority. */
internal object DoomRules {
    fun valid(game: StandaloneGame): Boolean {
        val bag = game.doomBag
        if (!game.profile.bagOfDoom || game.state == com.turnhub.android.protocol.TableState.LOBBY)
            return bag == DoomBag()
        val count = game.players.size
        if (count !in 5..StandaloneGame.MAX_PLAYERS || game.twoHeadedGiant) return false
        val all = (1 shl count) - 1
        val living = game.players.indices.filter { !game.players[it].eliminated }.fold(0) { mask, i -> mask or bit(i + 1) }
        if (bag.cycle !in 1..0xffffffffL || bag.lastCheck !in 0 until bag.cycle || bag.warmup !in 0..4 ||
            bag.lastDraw !in 0..count || bag.atRisk and living.inv() != 0 || bag.drawn and all.inv() != 0 ||
            bag.drawn and living != 0 || bag.selected != 0 || bag.sealed) return false
        if (bag.lastDraw != 0 && bag.drawn and bit(bag.lastDraw) == 0) return false
        if (game.players.indices.any { bag.drawn and bit(it + 1) != 0 &&
                (game.players[it].conceded || game.players[it].eliminatedBy != null) }) return false
        if (Integer.bitCount(living) <= 4) return bag.atRisk == 0 && bag.boundary == 0 && bag.warmup == 0 &&
            !bag.pending && bag.pendingTurn == 0 && bag.selected == 0
        if (living and bit(bag.boundary) == 0) return false
        if (bag.pending && (game.state != com.turnhub.android.protocol.TableState.PAUSED || bag.warmup != 0 || bag.pendingTurn !in 1..count)) return false
        if (!bag.pending && (bag.pendingTurn != 0 || bag.selected != 0)) return false
        return bag.selected == 0 || bag.contains(bag.selected)
    }

    fun bit(player: Int): Int = if (player in 1..16) 1 shl (player - 1) else 0
    fun previous(player: Int, living: Int, count: Int): Int = (1..count)
        .map { (player - 1 - it + count) % count + 1 }.firstOrNull { living and bit(it) != 0 } ?: 0
    fun start(starter: Int, living: Int, count: Int) = DoomBag(atRisk = living, boundary = previous(starter, living, count), warmup = 4, cycle = 1)
    fun pass(bag: DoomBag, player: Int): DoomBag {
        if (bag.cycle == 0L || bag.boundary != player) return bag
        val left = (bag.warmup - 1).coerceAtLeast(0)
        return bag.copy(warmup = left, pending = left == 0, pendingTurn = if (left == 0) player else 0)
    }
    fun leave(bag: DoomBag, living: Int, count: Int, credited: Int = 0): DoomBag {
        if (bag.cycle == 0L) return bag
        if (Integer.bitCount(living) <= 4) return bag.copy(atRisk = 0, boundary = 0, warmup = 0,
            pending = false, pendingTurn = 0, selected = 0)
        if (credited != 0) return bag.copy(atRisk = living and bit(credited).inv(),
            boundary = previous(credited, living, count), warmup = 0, pending = false, pendingTurn = 0,
            selected = 0, cycle = bag.cycle + 1)
        return bag.copy(atRisk = bag.atRisk and living,
            boundary = if (living and bit(bag.boundary) != 0) bag.boundary else previous(bag.boundary, living, count))
    }
    fun checked(bag: DoomBag, victim: Int, living: Int, count: Int): DoomBag =
        leave(bag, living, count).copy(atRisk = if (Integer.bitCount(living) > 4) living else 0,
            cycle = bag.cycle + 1, warmup = 0, pending = false, pendingTurn = 0, selected = 0,
            sealed = false, drawn = bag.drawn or bit(victim), lastDraw = victim, lastCheck = bag.cycle)
}

internal fun DoomBag.toSavedJson() = JSONObject().put("atRisk", atRisk).put("drawn", drawn).put("boundary", boundary)
    .put("warmup", warmup).put("cycle", cycle).put("pending", pending).put("pendingTurn", pendingTurn)
    .put("selected", selected).put("lastDraw", lastDraw).put("lastCheck", lastCheck)
internal fun doomFromJson(j: JSONObject): DoomBag = DoomBag(
    atRisk = j.getInt("atRisk"), drawn = j.optInt("drawn"), boundary = j.getInt("boundary"), warmup = j.getInt("warmup"),
    cycle = j.getLong("cycle"), pending = j.getBoolean("pending"), pendingTurn = j.getInt("pendingTurn"),
    selected = j.optInt("selected"), sealed = j.optBoolean("sealed"),
    lastDraw = j.getInt("lastDraw"), lastCheck = j.getLong("lastCheck"),
)
