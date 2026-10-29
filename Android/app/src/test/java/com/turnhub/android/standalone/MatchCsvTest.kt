package com.turnhub.android.standalone

import com.turnhub.android.testing.Fixtures
import org.junit.Assert.assertEquals
import org.junit.Test

class MatchCsvTest {
    /** The shared fixture: the core's recordCsvRows gives this exact text too (core/tests). */
    @Test fun `a match record gives the same CSV as the shared core`() {
        val record = Fixtures.json("match-record.json")
        assertEquals(Fixtures.text("match-record.csv"), MatchCsv.HEADER + MatchCsv.rows(record))
    }

    @Test fun `the app's own records round-trip into the same CSV`() {
        val record = requireNotNull(GameRecord.fromJson(Fixtures.json("match-record.json")))
        assertEquals(Fixtures.text("match-record.csv"), MatchCsv.of(listOf(record)))
    }
}
