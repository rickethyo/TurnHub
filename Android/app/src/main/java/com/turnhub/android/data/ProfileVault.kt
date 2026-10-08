package com.turnhub.android.data

import android.content.Context
import android.os.Build
import android.security.keystore.KeyGenParameterSpec
import android.security.keystore.KeyProperties
import android.util.Base64
import androidx.core.content.edit
import java.security.KeyStore
import javax.crypto.Cipher
import javax.crypto.KeyGenerator
import javax.crypto.SecretKey
import javax.crypto.spec.GCMParameterSpec

/** The account this phone signs in to an Atlas automatically. The secret itself never leaves [ProfileVault]. */
data class SavedProfile(val atlasId: String, val profileId: String, val name: String)

/**
 * App lock and automatic sign-in (STAGED_CHANGES "App lock and automatic
 * sign-in", V1 phase 7). One profile per Atlas: its PIN or password,
 * encrypted with a key that Android releases only for a short time after the
 * phone's fingerprint, face or screen lock confirms the owner. The caller
 * runs that check first (MainActivity's prompt), then calls [save] or
 * [unlock] within [AUTH_SECONDS].
 */
interface ProfileVault {
    /** False on phones older than Android 11, where the key cannot accept the screen lock too. */
    val available: Boolean
    fun saved(atlasId: String): SavedProfile?
    /** Encrypts and stores [secret]; false if the phone's check is no longer fresh. */
    fun save(profile: SavedProfile, secret: String): Boolean
    /** The stored secret, or null (no entry, check not fresh, or the key was invalidated). */
    fun unlock(atlasId: String): String?
    fun forget(atlasId: String)

    companion object {
        const val AUTH_SECONDS = 30
    }
}

/**
 * [ProfileVault] in the Android Keystore (AES-GCM, hardware-backed where the
 * phone has it) plus app-private preferences for the ciphertext. Excluded from
 * backup and device transfer like the Wi-Fi passwords. Adding a fingerprint or
 * removing the screen lock invalidates the key; the entry is then forgotten
 * and the player signs in by hand once more.
 */
class KeystoreProfileVault(context: Context) : ProfileVault {

    private val prefs = context.applicationContext.getSharedPreferences(FILE, Context.MODE_PRIVATE)

    override val available: Boolean = Build.VERSION.SDK_INT >= Build.VERSION_CODES.R

    override fun saved(atlasId: String): SavedProfile? {
        if (!available) return null
        val profileId = prefs.getString("$atlasId:profile", null) ?: return null
        return SavedProfile(atlasId, profileId, prefs.getString("$atlasId:name", null) ?: profileId)
    }

    override fun save(profile: SavedProfile, secret: String): Boolean {
        if (!available) return false
        return try {
            val cipher = Cipher.getInstance(TRANSFORMATION)
            cipher.init(Cipher.ENCRYPT_MODE, key())
            val sealed = cipher.doFinal(secret.toByteArray(Charsets.UTF_8))
            prefs.edit {
                putString("${profile.atlasId}:profile", profile.profileId)
                putString("${profile.atlasId}:name", profile.name)
                putString("${profile.atlasId}:iv", encode(cipher.iv))
                putString("${profile.atlasId}:secret", encode(sealed))
            }
            true
        } catch (e: Exception) {
            // Not authenticated recently, or the key was invalidated (new fingerprint).
            if (e is android.security.keystore.KeyPermanentlyInvalidatedException) dropKey()
            false
        }
    }

    override fun unlock(atlasId: String): String? {
        if (!available) return null
        val iv = prefs.getString("$atlasId:iv", null)?.let(::decode) ?: return null
        val sealed = prefs.getString("$atlasId:secret", null)?.let(::decode) ?: return null
        return try {
            val cipher = Cipher.getInstance(TRANSFORMATION)
            cipher.init(Cipher.DECRYPT_MODE, existingKey() ?: return null, GCMParameterSpec(128, iv))
            String(cipher.doFinal(sealed), Charsets.UTF_8)
        } catch (_: android.security.keystore.KeyPermanentlyInvalidatedException) {
            dropKey()
            forgetAll()
            null
        } catch (_: Exception) {
            null // Not authenticated recently; the caller asks again next time.
        }
    }

    override fun forget(atlasId: String) {
        prefs.edit {
            listOf("profile", "name", "iv", "secret").forEach { remove("$atlasId:$it") }
        }
    }

    private fun forgetAll() = prefs.edit { clear() }

    private fun keyStore(): KeyStore = KeyStore.getInstance(KEYSTORE).apply { load(null) }

    private fun existingKey(): SecretKey? = keyStore().getKey(KEY_ALIAS, null) as? SecretKey

    private fun dropKey() {
        runCatching { keyStore().deleteEntry(KEY_ALIAS) }
    }

    private fun key(): SecretKey {
        // Keep the platform requirement explicit at the API boundary, even if
        // a future caller bypasses the public availability check.
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.R) {
            throw IllegalStateException("Profile app lock requires Android 11 or later")
        }
        return existingKey() ?: KeyGenerator.getInstance(KeyProperties.KEY_ALGORITHM_AES, KEYSTORE).run {
            init(
                KeyGenParameterSpec.Builder(KEY_ALIAS, KeyProperties.PURPOSE_ENCRYPT or KeyProperties.PURPOSE_DECRYPT)
                    .setBlockModes(KeyProperties.BLOCK_MODE_GCM)
                    .setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE)
                    .setKeySize(256)
                    .setUserAuthenticationRequired(true)
                    .setUserAuthenticationParameters(
                        ProfileVault.AUTH_SECONDS,
                        KeyProperties.AUTH_BIOMETRIC_STRONG or KeyProperties.AUTH_DEVICE_CREDENTIAL,
                    )
                    .setInvalidatedByBiometricEnrollment(true)
                    .build(),
            )
            generateKey()
        }
    }

    private fun encode(bytes: ByteArray) = Base64.encodeToString(bytes, Base64.NO_WRAP)
    private fun decode(text: String) = Base64.decode(text, Base64.NO_WRAP)

    companion object {
        /** Must match the exclusions in res/xml/backup_rules.xml and data_extraction_rules.xml. */
        const val FILE = "profile_vault"
        private const val KEYSTORE = "AndroidKeyStore"
        private const val KEY_ALIAS = "turnhub_profile_vault"
        private const val TRANSFORMATION = "AES/GCM/NoPadding"
    }
}
