package com.turnhub.android.protocol

/**
 * Mirrors one entry of `players[]` in protocol/state-v0.1.schema.json.
 *
 * Only the fields this milestone's mocked home screen renders are modeled here.
 * `commanderDamage` and `lifeRequest` are part of the real schema (see
 * protocol/state-v0.1.schema.json and
 * Documentation/engineering/LIFE_APPROVAL_AND_COMMANDER.md) but are out of scope
 * for a controller-summary mock; they are left out rather than guessed at, and
 * should be added once a screen actually needs to render them.
 */
data class Player(
    val playerNumber: Int,
    val moduleId: Int,
    val slot: Int,
    val profileId: String?,
    val displayName: String?,
    val eliminated: Boolean,
    val life: Int?,
    val turnsCompleted: Int,
)
