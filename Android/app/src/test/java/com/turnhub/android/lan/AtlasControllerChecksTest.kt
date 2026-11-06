package com.turnhub.android.lan

import kotlinx.coroutines.*
import kotlinx.coroutines.test.*
import org.junit.Assert.*
import org.junit.Test

@OptIn(ExperimentalCoroutinesApi::class)
class AtlasControllerChecksTest {
    @Test fun `a slow verification gets five useful seconds after the response`() = runTest {
        var calls=0
        val checks=AtlasControllerChecks(backgroundScope,{testScheduler.currentTime}) { _,_ ->
            if (++calls==1) delay(6000)
            "ABCDEF01"
        }
        val first=async { checks.verify("phone","one") }; runCurrent()
        advanceTimeBy(6000); runCurrent(); assertEquals("ABCDEF01",first.await())
        assertEquals("ABCDEF01",checks.verify("phone","one")); assertEquals(1,calls)
        advanceTimeBy(4999); assertEquals("ABCDEF01",checks.verify("phone","one")); assertEquals(1,calls)
        advanceTimeBy(2); assertEquals("ABCDEF01",checks.verify("phone","one")); assertEquals(2,calls)
    }

    @Test fun `cached phones do not wait for another phones check and cancelled callers do not cancel shared verification`() = runTest {
        val release=CompletableDeferred<Unit>(); var slowCalls=0
        val checks=AtlasControllerChecks(backgroundScope,{testScheduler.currentTime}) { _,value ->
            if (value=="slow") { slowCalls++; release.await() }
            "ABCDEF01"
        }
        assertEquals("ABCDEF01",checks.verify("phone","warm"))
        val first=async { checks.verify("phone","slow") }; runCurrent()
        val second=async { checks.verify("phone","slow") }; runCurrent()
        assertEquals("ABCDEF01",withTimeout(10) { checks.verify("phone","warm") })
        first.cancel(); runCurrent(); release.complete(Unit)
        assertEquals("ABCDEF01",second.await()); assertEquals(1,slowCalls)
    }

    @Test fun `concurrent failures share one check and do not poison the next attempt`() = runTest {
        var calls=0
        val checks=AtlasControllerChecks(backgroundScope,{testScheduler.currentTime}) { _,_ ->
            if (++calls==1) { delay(100); throw java.io.IOException("temporary") }
            "ABCDEF01"
        }
        val one=async { runCatching { checks.verify("phone","one") } }
        val two=async { runCatching { checks.verify("phone","one") } }
        runCurrent(); advanceTimeBy(100); runCurrent()
        assertTrue(one.await().isFailure); assertTrue(two.await().isFailure); assertEquals(1,calls)
        assertEquals("ABCDEF01",checks.verify("phone","one")); assertEquals(2,calls)
    }

    @Test fun `expired authorization is refused immediately without using the old grant`() = runTest {
        var allowed=true
        val checks=AtlasControllerChecks(backgroundScope,{testScheduler.currentTime}) { _,_ -> if (allowed) "ABCDEF01" else null }
        assertEquals("ABCDEF01",checks.verify("phone","one"))
        allowed=false; advanceTimeBy(5000)
        assertNull(checks.verify("phone","one"))
        allowed=true
        assertEquals("ABCDEF01",checks.verify("phone","one"))
    }
}
