package com.turnhub.android.ui.home

import com.turnhub.android.protocol.GameProfile
import org.junit.Assert.assertEquals
import org.junit.Test

class LifeStepsTest {

    @Test
    fun yugiohCountsInHundreds() {
        assertEquals(LifeSteps(100, 1000), lifeSteps(GameProfile.YUGIOH))
    }

    @Test
    fun otherProfilesCountInOnesAndFives() {
        GameProfile.entries.filter { it != GameProfile.YUGIOH }.forEach {
            assertEquals(LifeSteps(1, 5), lifeSteps(it))
        }
    }
}
