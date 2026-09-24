package com.turnhub.android.data

import com.turnhub.android.protocol.AtlasConnectionState
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * Exercises [MockAtlasRepository] as a plain JVM unit test -- no Android
 * framework/Robolectric needed, which is itself a small proof that the
 * data/UI split in Android/README.md's architecture rule is real: this
 * repository doesn't touch anything Android-specific.
 */
@OptIn(ExperimentalCoroutinesApi::class)
class MockAtlasRepositoryTest {

    @Test
    fun `starts disconnected with no table or sigils`() {
        val repository = MockAtlasRepository()

        assertEquals(AtlasConnectionState.DISCONNECTED, repository.connectionState.value)
        assertEquals(null, repository.tableSummary.value)
        assertTrue(repository.sigils.value.isEmpty())
    }

    @Test
    fun `connect transitions to connected and populates mock data`() = runTest {
        val repository = MockAtlasRepository()

        repository.connect()

        assertEquals(AtlasConnectionState.CONNECTED, repository.connectionState.value)
        assertNotNull(repository.tableSummary.value)
        assertTrue(repository.sigils.value.isNotEmpty())
    }

    @Test
    fun `disconnect clears table and sigils`() = runTest {
        val repository = MockAtlasRepository()
        repository.connect()

        repository.disconnect()

        assertEquals(AtlasConnectionState.DISCONNECTED, repository.connectionState.value)
        assertEquals(null, repository.tableSummary.value)
        assertTrue(repository.sigils.value.isEmpty())
    }
}
