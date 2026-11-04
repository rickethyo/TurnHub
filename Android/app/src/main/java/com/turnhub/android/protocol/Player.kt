package com.turnhub.android.protocol

/**
 * Commander damage this player has received from [sourcePlayer]'s commanders.
 * [damage] always has two entries: commander 1 and commander 2.
 */
data class CommanderDamage(
    val sourcePlayer: Int,
    val damage: List<Int>,
)

/** Where a life-change request stands. */
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
 * A request by [actor] to change [target]'s life by [delta]. The game owns the
 * approval deadline ([windowMs] after [requestedAtMs], the recipient's own choice).
 */
data class LifeRequest(
    /** Unsigned 32-bit. */
    val id: Long,
    val actor: Int,
    val target: Int,
    val delta: Int,
    val state: LifeRequestState,
    /** The game's clock when it was asked; only comparable within one game. */
    val requestedAtMs: Long,
    /** The recipient's approval window in milliseconds (15,000 by default). */
    val windowMs: Long = 15_000L,
)
