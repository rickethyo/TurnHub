package com.turnhub.android.data

/** The hub owns the room; Atlas only supplies paired controllers with access. */
interface AtlasRoomLink {
    val isAtlasGuest: Boolean
    suspend fun publishedInvitation(): String?
    fun offerAtlasRoom(invitation: String, ticket: String)
}
