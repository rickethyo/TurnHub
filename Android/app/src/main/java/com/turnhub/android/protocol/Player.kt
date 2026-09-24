package com.turnhub.android.protocol

/**
 * Mirrors one entry of `players[]` in protocol/state-v0.1.schema.json.
 *
 * [moduleId] keeps the wire spelling for an Atlas *controller handle*
 * (physical 0-7, virtual 8-23; protocol/http-v1.md). It is not a durable
 * device ID and not a paired-Sigil inventory entry.
 *
 * [profileId] and [displayName] are optional in the schema; current Atlas
 * firmware does not send them in `/api/v1/state`, so both are normally null.
 */
data class Player(
    val playerNumber: Int,
    val moduleId: Int,
    /** 1 = the module's primary seat (A), 2 = its secondary seat (B). */
    val slot: Int,
    /** Unsigned 32-bit; stable only while this participant stays at the table. */
    val participantId: Long,
    val profileId: String?,
    val displayName: String?,
    val eliminated: Boolean,
    /** Null in the lobby, before Atlas has captured starting life. */
    val life: Int?,
    /** Unsigned 32-bit. */
    val turnsCompleted: Long,
    /** Missing sources mean zero damage (protocol/http-v1.md). */
    val commanderDamage: List<CommanderDamage>,
    val lifeRequest: LifeRequest?,
)

/**
 * Commander damage this player has received from [sourcePlayer]'s commanders.
 * [damage] always has two entries: commander 1 and commander 2.
 */
data class CommanderDamage(
    val sourcePlayer: Int,
    val damage: List<Int>,
)

/** Mirrors `lifeRequest.state` in protocol/state-v0.1.schema.json. */
enum class LifeRequestState {
    NONE,
    PENDING,
    ACCEPTED,
    REJECTED,
    AUTOMATIC,
    CANCELLED,
    FAILED,
    ;

    companion object {
        fun fromWire(value: String): LifeRequestState? = entries.firstOrNull { it.name == value }
    }
}

/**
 * A request by [actor] to change [target]'s life by [delta]. Atlas owns the
 * approval deadline (15,000 ms after [requestedAtMs] under its current rules);
 * this app only renders the state Atlas reports.
 */
data class LifeRequest(
    /** Unsigned 32-bit. */
    val id: Long,
    val actor: Int,
    val target: Int,
    val delta: Int,
    val state: LifeRequestState,
    /** Atlas uptime clock, unsigned 32-bit, wraps; only comparable within one boot. */
    val requestedAtMs: Long,
)
