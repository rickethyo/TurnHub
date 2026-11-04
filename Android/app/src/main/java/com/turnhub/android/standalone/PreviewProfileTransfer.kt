package com.turnhub.android.standalone

import org.json.JSONArray
import org.json.JSONObject

/** An explicit snapshot, never a shared store or an ongoing sync between app variants. */
object PreviewProfileTransfer {
    const val TURNHUB = "com.turnhub.android"
    const val PREVIEW = "$TURNHUB.preview"
    const val ACTIVITY = "$TURNHUB.ProfileTransferActivity"
    const val ACTION = "$TURNHUB.COPY_PROFILES_TO_PREVIEW"
    const val MAX_BYTES = 16 * 1024 * 1024
    const val MAX_ACCOUNTS = 256

    fun read(input: java.io.InputStream, maxBytes: Int = MAX_BYTES): JSONObject {
        val out = java.io.ByteArrayOutputStream()
        val buffer = ByteArray(16 * 1024)
        while (true) {
            val count = input.read(buffer)
            if (count < 0) break
            require(out.size().toLong() + count <= maxBytes) { "The profile copy is too large." }
            out.write(buffer, 0, count)
        }
        return JSONObject(out.toString(Charsets.UTF_8.name()))
    }

    /**
     * SHA-256 of the certificates allowed to sign TurnHub Preview (the owner's
     * debug key). A package name alone can be claimed by any sideloaded app.
     * Public fingerprints, not secrets.
     */
    val PREVIEW_SIGNERS: Set<String> = setOf(
        "42621edb537ab6b3c0f4f02d61279af55f5e788e7e7b0efd47c36b66ed00ac0b", // Legion7-Pro debug.keystore (2026-11-04)
    )

    /** [callerSigners]: SHA-256 hex of each certificate the calling package is signed with. */
    fun allowedCaller(source: String, caller: String?, action: String?, callerSigners: Set<String>,
        trusted: Set<String> = PREVIEW_SIGNERS): Boolean =
        source == TURNHUB && caller == PREVIEW && action == ACTION &&
            callerSigners.isNotEmpty() && callerSigners.all { it.lowercase() in trusted.map(String::lowercase) }

    fun sha256Hex(bytes: ByteArray): String =
        java.security.MessageDigest.getInstance("SHA-256").digest(bytes).joinToString("") { "%02x".format(it) }

    /** Each protected account must be unlocked before its verifier/history/links leave the source. */
    fun export(library: LocalLibrary, selected: Set<String>, secrets: Map<String, String>): JSONObject {
        require(selected.isNotEmpty() && selected.size <= MAX_ACCOUNTS) { "Select at least one account." }
        val players = library.players.filter { it.localId in selected }
        require(players.size == selected.size) { "An account changed. Select it again." }
        players.forEach { player ->
            require(player.credential?.verifies(secrets[player.localId].orEmpty()) != false) {
                "Check the PIN or password for ${player.name}."
            }
        }
        val admin = players.firstOrNull { it.localId == library.deviceAdmin }?.profileId
        return JSONObject().put("transferSchema", 1).put("source", TURNHUB)
            .put("adminProfileId", admin ?: JSONObject.NULL)
            .put("accounts", JSONArray().apply {
                players.forEach { put(AccountCopy.toJson(it, library.history)) }
            }).also { require(it.toString().toByteArray(Charsets.UTF_8).size <= MAX_BYTES) { "The selected history is too large to copy at once. Select fewer accounts." } }
    }

    /** Validate the whole envelope before any destination write. */
    fun accounts(bundle: JSONObject): List<JSONObject> {
        require(bundle.getInt("transferSchema") == 1 && bundle.getString("source") == TURNHUB)
        val list = bundle.getJSONArray("accounts")
        require(list.length() in 1..MAX_ACCOUNTS)
        return (0 until list.length()).map { list.getJSONObject(it) }.also { accounts ->
            val ids = accounts.map { AccountCopy.fromJson(it, "validate").profileId }
            require(ids.distinct().size == ids.size)
            val admin = bundle.optStringOrNull("adminProfileId")
            require(admin == null || admin in ids)
        }
    }
}
