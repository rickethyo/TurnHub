package com.turnhub.android.data

import android.content.Context

/** One unconfirmed outage is retained until Atlas acknowledges it. No account secrets. */
interface OfflineQueueStore {
    fun load(): String?
    fun save(record: String?): Boolean
}
class MemoryOfflineQueueStore : OfflineQueueStore {
    private var record: String? = null
    override fun load() = record
    override fun save(record: String?): Boolean { this.record = record; return true }
}
class PreferencesOfflineQueueStore(context: Context) : OfflineQueueStore {
    private val prefs = context.getSharedPreferences("atlas_offline", Context.MODE_PRIVATE)
    override fun load(): String? = prefs.getString("pending", null)
    override fun save(record: String?) = prefs.edit().apply {
        if (record == null) remove("pending") else putString("pending", record)
    }.commit()
}
