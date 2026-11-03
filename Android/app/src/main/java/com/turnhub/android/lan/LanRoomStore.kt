package com.turnhub.android.lan

import android.content.Context
import android.security.keystore.KeyGenParameterSpec
import android.security.keystore.KeyProperties
import java.security.KeyStore
import java.util.Base64
import javax.crypto.Cipher
import javax.crypto.KeyGenerator
import javax.crypto.SecretKey
import javax.crypto.spec.GCMParameterSpec
import org.json.JSONObject

/** Room and reconnect capabilities are encrypted with a non-exportable device key. */
internal class LanRoomStore(context: Context) {
    private val prefs = context.getSharedPreferences("turnhub_rooms", Context.MODE_PRIVATE)
    private fun key(): SecretKey {
        val alias = "turnhub_room_v2"
        val store = KeyStore.getInstance("AndroidKeyStore").apply { load(null) }
        (store.getKey(alias, null) as? SecretKey)?.let { return it }
        return KeyGenerator.getInstance(KeyProperties.KEY_ALGORITHM_AES, "AndroidKeyStore").apply {
            init(KeyGenParameterSpec.Builder(alias, KeyProperties.PURPOSE_ENCRYPT or KeyProperties.PURPOSE_DECRYPT)
                .setBlockModes(KeyProperties.BLOCK_MODE_GCM).setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE).build())
        }.generateKey()
    }
    fun read(name: String): JSONObject? = runCatching {
        val raw = prefs.getString(name, null) ?: return null
        val bytes = Base64.getDecoder().decode(raw)
        val cipher = Cipher.getInstance("AES/GCM/NoPadding")
        cipher.init(Cipher.DECRYPT_MODE, key(), GCMParameterSpec(128, bytes.copyOfRange(0, 12)))
        JSONObject(String(cipher.doFinal(bytes.copyOfRange(12, bytes.size)), Charsets.UTF_8))
    }.getOrNull()
    fun write(name: String, record: JSONObject?): Boolean = runCatching {
        if (record == null) return prefs.edit().remove(name).commit()
        val cipher = Cipher.getInstance("AES/GCM/NoPadding").apply { init(Cipher.ENCRYPT_MODE, key()) }
        val encoded = Base64.getEncoder().encodeToString(cipher.iv + cipher.doFinal(record.toString().toByteArray(Charsets.UTF_8)))
        prefs.edit().putString(name, encoded).commit()
    }.getOrDefault(false)
}
