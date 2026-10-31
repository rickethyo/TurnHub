package com.turnhub.android.domain

import com.turnhub.android.protocol.ProfileSummary
import com.turnhub.android.standalone.LocalLibrary
import com.turnhub.android.standalone.SavedLocalPlayer

/** Identity, never display-name equality, decides whether copies are one row. */
data class UnifiedProfile(val local: SavedLocalPlayer?, val atlas: ProfileSummary?) {
    val name get() = local?.name ?: requireNotNull(atlas).name
    val key get() = local?.let { "local:${it.localId}" } ?: "atlas:${atlas!!.profileId}"
    val location get() = when { local != null && atlas != null -> "This device · Atlas"; local != null -> "This device"; else -> "Atlas" }
}
fun unifiedProfiles(library: LocalLibrary, atlasId: String?, remote: List<ProfileSummary>): List<UnifiedProfile> {
    val locals = library.players.map { local ->
        UnifiedProfile(local, remote.firstOrNull { it.profileId == local.profileId ||
            (atlasId != null && library.linkedPlayer(atlasId, it.profileId)?.localId == local.localId) })
    }
    val represented = locals.mapNotNull { it.atlas?.profileId }.toSet()
    return (locals + remote.filter { it.profileId !in represented }.map { UnifiedProfile(null, it) })
        .sortedWith(compareBy<UnifiedProfile> { it.name.lowercase() }.thenBy { it.key })
}
