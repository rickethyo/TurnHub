package com.turnhub.android.lan

import com.turnhub.android.standalone.PlanarFace
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test

class PlanarRollFeedbackTest {
    @Test fun `only accepted action receipts confirm valid faces`() {
        for (face in PlanarFace.entries) {
            val reply = JSONObject().put("code", "ok").put("rollResult", JSONObject().put("face", face.wireValue))
            assertEquals(PlanarRollFeedback("one", "confirmed", face), confirmedPlanarFeedback("one", reply))
            for (code in listOf("rejected", "stale", "forbidden", "interrupted")) {
                assertNull(confirmedPlanarFeedback("one", JSONObject(reply.toString()).put("code", code)).face)
            }
        }
        assertEquals("accepted", confirmedPlanarFeedback("two", JSONObject().put("code", "ok")).status)
        assertEquals("uncertain", confirmedPlanarFeedback("two", JSONObject().put("code", "unavailable")).status)
        assertNull(confirmedPlanarFeedback("two", JSONObject().put("code", "ok").put("rollResult", JSONObject().put("face", "six"))).face)
    }

    @Test fun `phone host records the real face of an accepted roll`() {
        val local = JSONObject().put("code", "ok").put("game", JSONObject().put("planechase",
            JSONObject().put("lastRoll", JSONObject().put("face", "planeswalk"))))
        assertEquals("planeswalk", recordedPlanarRoll("planar-roll", local)?.text("face"))
        assertNull(recordedPlanarRoll("planar-roll", JSONObject(local.toString()).put("code", "stale")))
        assertNull(recordedPlanarRoll("life", local))
    }
}
