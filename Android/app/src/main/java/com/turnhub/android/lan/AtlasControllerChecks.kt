package com.turnhub.android.lan

import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.CoroutineStart
import kotlinx.coroutines.Deferred
import kotlinx.coroutines.NonCancellable
import kotlinx.coroutines.async
import kotlinx.coroutines.withContext
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock

/** Coalesce each phone's verification without blocking other phones' already-valid cache hits. */
internal class AtlasControllerChecks(
    private val scope: CoroutineScope,
    private val clock: () -> Long,
    private val fetch: suspend (String, String) -> String?,
) {
    private data class Verified(val atMs: Long, val phone: String)
    private val mutex = Mutex()
    private val cached = linkedMapOf<Pair<String,String>, Verified>()
    private val pending = mutableMapOf<Pair<String,String>, Deferred<Result<String?>>>()

    suspend fun verify(field: String, value: String): String? {
        val key = field to value
        val check = mutex.withLock {
            cached[key]?.takeIf { clock()-it.atMs < 5000 }?.let { return it.phone }
            pending[key]?.let { return@withLock it }
            lateinit var created: Deferred<Result<String?>>
            created = scope.async(start = CoroutineStart.LAZY) {
                try {
                    val phone = fetch(field,value)
                    mutex.withLock {
                        if (phone == null) cached.remove(key)
                        else {
                            if (cached.size >= 32 && key !in cached) cached.remove(cached.keys.first())
                            cached[key] = Verified(clock(),phone)
                        }
                    }
                    Result.success(phone)
                } catch (e: CancellationException) { throw e }
                  catch (e: Exception) { Result.failure(e) }
                finally {
                    withContext(NonCancellable) { mutex.withLock { if (pending[key] === created) pending.remove(key) } }
                }
            }
            pending[key] = created
            created
        }
        try { return check.await().getOrThrow() }
        finally {
            if (check.isCompleted) withContext(NonCancellable) {
                mutex.withLock { if (pending[key] === check) pending.remove(key) }
            }
        }
    }
}
