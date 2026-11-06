package com.turnhub.android.data

import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock

/** Atlas serves HTTPS serially. Share a cancellable lane across transport instances on this phone. */
internal object AtlasRequestLane {
    private class Entry(val mutex: Mutex = Mutex(), var users: Int = 0)
    private val entries = mutableMapOf<String, Entry>()

    suspend fun <T> run(endpoint: String, block: suspend () -> T): T {
        val entry = synchronized(entries) { entries.getOrPut(endpoint) { Entry() }.also { it.users++ } }
        try { return entry.mutex.withLock { block() } }
        finally { synchronized(entries) { if (--entry.users == 0) entries.remove(endpoint) } }
    }
}
