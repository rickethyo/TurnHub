package com.turnhub.android.standalone

import org.json.JSONObject

/** Configuration for a new game. Never retains scores, clocks, wins or account secrets. */
data class SavedGameSetup(val name: String, val game: StandaloneGame) {
    fun toJson(): JSONObject = JSONObject().put("name", name)
        .put("game", StandaloneTable.gameToJson(freshSetup(game)))

    companion object {
        const val MAX_NAME = 40
        fun fromJson(json: JSONObject): SavedGameSetup {
            val name = json.getString("name")
            require(name.isNotBlank() && name.length <= MAX_NAME)
            return SavedGameSetup(name, freshSetup(StandaloneTable.gameFromJson(json.getJSONObject("game"))))
        }
    }
}

internal fun freshSetup(game: StandaloneGame): StandaloneGame = StandaloneGame(
    profile = game.profile, startingLife = game.startingLife,
    autoEliminateAtZeroLife = game.autoEliminateAtZeroLife, askEliminationSource = game.askEliminationSource,
    autoEliminateAtPoisonLimit = game.autoEliminateAtPoisonLimit,
    autoEliminateAtCommanderDamage = game.autoEliminateAtCommanderDamage, poisonLimit = game.poisonLimit,
    turnTimerMs = game.turnTimerMs, timerRules = game.timerRules,
    twoHeadedGiant = game.twoHeadedGiant, houseRules = game.houseRules,
    useRelics = game.useRelics, drawTwoCards = game.drawTwoCards,
    players = game.players.map { LocalPlayer(name = it.name, localId = it.localId, profileId = it.profileId,
        guest = it.guest, commander = it.commander, commanderArt = it.commanderArt,
        cardOnly = it.cardOnly, investigator = it.investigator) },
    starter = game.starter,
    planechase = Planechase(enabled = game.planechase.enabled, source = game.planechase.source,
        selection = game.planechase.selection, phenomena = game.planechase.phenomena),
    elder = com.turnhub.android.domain.ElderOneTrack(elderOne = game.elder.elderOne, episode = game.elder.episode),
)
