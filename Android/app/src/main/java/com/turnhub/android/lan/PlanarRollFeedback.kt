package com.turnhub.android.lan

import com.turnhub.android.standalone.PlanarFace
import org.json.JSONObject

internal data class PlanarRollFeedback(val actionId: String, val status: String, val face: PlanarFace? = null)

internal fun planarActionsAllowed(state: LanUiState, summary: com.turnhub.android.domain.TableSummary): Boolean =
    state.online && !state.busy && !state.uncertain && summary.settings.planechase &&
        summary.settings.profile.magicRules && (state.master || state.role == "table" ||
            state.role == "player" && summary.players.any { it.playerNumber == state.player && !it.eliminated })

internal fun planarRollAllowed(state: LanUiState, summary: com.turnhub.android.domain.TableSummary): Boolean =
    planarActionsAllowed(state, summary) && summary.state == com.turnhub.android.protocol.TableState.RUNNING &&
        (if (state.role == "player") summary.hasTurn(state.player) else state.master || state.role == "table")

/** Capture the accepted action's result, not whichever roll a later snapshot happens to show. */
internal fun recordedPlanarRoll(kind: String, result: JSONObject): JSONObject? {
    if (result.optString("code") != "ok" || !isPlanarRollKind(kind)) return null
    val face = result.optJSONObject("game")?.optJSONObject("planechase")?.optJSONObject("lastRoll")?.optString("face")
    return PlanarFace.fromWire(face.orEmpty())?.let { JSONObject().put("face", it.wireValue) }
}

internal fun confirmedPlanarFeedback(id: String, reply: JSONObject): PlanarRollFeedback {
    val code = reply.optString("code")
    val face = if (code == "ok") PlanarFace.fromWire(reply.optJSONObject("rollResult")?.optString("face").orEmpty()) else null
    return PlanarRollFeedback(id, when {
        code == "ok" && face != null -> "confirmed"
        code == "ok" -> "accepted"
        code == "unavailable" -> "uncertain"
        else -> code
    }, face)
}
