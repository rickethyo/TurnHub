package com.turnhub.android.standalone

import org.json.JSONObject

/** Paired-device profile copy. No device role, account-link key, image or history. */
object SharedProfileCopy {
    fun encode(player: SavedLocalPlayer): JSONObject = AccountCopy.toJson(player.copy(
        links = emptyList(), commanders = emptyList(),
        personalization = LocalPersonalization(color = player.personalization.color,
            avatar = player.personalization.avatar, reduceMotion = player.personalization.reduceMotion),
    ))

    fun decode(json: JSONObject, localId: String): SavedLocalPlayer = AccountCopy.fromJson(json, localId).let { copy ->
        copy.copy(links = emptyList(),commanders = emptyList(),personalization = LocalPersonalization(
            color=copy.personalization.color,avatar=copy.personalization.avatar,reduceMotion=copy.personalization.reduceMotion))
    }
}
