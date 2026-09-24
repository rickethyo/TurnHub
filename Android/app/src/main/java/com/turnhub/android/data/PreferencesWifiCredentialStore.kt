package com.turnhub.android.data

import android.content.Context
import androidx.core.content.edit

/**
 * [WifiCredentialStore] in app-private SharedPreferences. The file is excluded
 * from Android backup and device transfer (res/xml/backup_rules.xml,
 * data_extraction_rules.xml), so saved Atlas passwords stay on this phone.
 */
class PreferencesWifiCredentialStore(context: Context) : WifiCredentialStore {

    private val prefs = context.applicationContext.getSharedPreferences(FILE, Context.MODE_PRIVATE)

    override fun lastSsid(): String? = prefs.getString(KEY_LAST_SSID, null)

    override fun load(ssid: String): WifiCredentials? =
        prefs.getString(passphraseKey(ssid), null)?.let { WifiCredentials(ssid, it) }

    override fun save(credentials: WifiCredentials) {
        prefs.edit {
            putString(passphraseKey(credentials.ssid), credentials.passphrase)
            putString(KEY_LAST_SSID, credentials.ssid)
        }
    }

    private fun passphraseKey(ssid: String) = "passphrase:$ssid"

    companion object {
        /** Must match the exclusions in res/xml/backup_rules.xml and data_extraction_rules.xml. */
        const val FILE = "atlas_wifi"
        private const val KEY_LAST_SSID = "last_ssid"
    }
}
