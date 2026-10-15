package com.turnhub.android.domain

/** Optional rules owned by the on-device game, never inferred for Atlas games. */
enum class TimeoutAction(val label: String, val autoPass: Boolean = false, val demerit: Boolean = false) {
    OVERTIME("Overtime"),
    PASS("Auto-pass", autoPass = true),
    DEMERIT("Demerit", demerit = true),
    PASS_AND_DEMERIT("Both", autoPass = true, demerit = true),
}

data class LocalTimerRules(
    val timeout: TimeoutAction = TimeoutAction.OVERTIME,
    val earnExtensions: Boolean = false,
    val earnBeforePercent: Int = 75,
    val maxEarnedPerGame: Int = 2,
) {
    init {
        require(earnBeforePercent == 75 || earnBeforePercent == 100)
        require(maxEarnedPerGame in 1..3)
    }
}
