package com.turnhub.android.protocol

import org.json.JSONException
import org.json.JSONObject

/** Atlas's first-run setup stage (Atlas `setup_stage.h`, FIRST_RUN_SETUP.md). */
enum class SetupStage(val wire: String) {
    /** New or factory-reset Atlas: the setup steps are due. */
    WELCOME("welcome"),

    /** A phone finished the steps; the Atlas screen says "You're all set". */
    FINISHED("finished"),

    /** Normal operation. */
    COMPLETE("complete"),
    ;

    companion object {
        fun fromWire(value: String): SetupStage? = entries.firstOrNull { it.wire == value }
    }
}

/**
 * `GET /api/setup`, readable before signing in. Never carries the Wi-Fi
 * password, only whether it is still the printed default.
 */
data class SetupStatus(
    val stage: SetupStage,
    val adminExists: Boolean,
    val passwordIsDefault: Boolean,
    val ssid: String,
) {
    companion object {

        /** Throws [AtlasWireException.Malformed] for a body that isn't the documented shape. */
        fun parse(body: String): SetupStatus {
            val root = try {
                JSONObject(body)
            } catch (e: JSONException) {
                throw AtlasWireException.Malformed("Setup status is not JSON")
            }
            val stage = SetupStage.fromWire(root.optString("stage"))
                ?: throw AtlasWireException.Malformed("Unknown setup stage '${root.optString("stage")}'")
            return SetupStatus(
                stage = stage,
                adminExists = root.optBoolean("adminExists"),
                passwordIsDefault = root.optBoolean("passwordIsDefault"),
                ssid = root.optString("ssid"),
            )
        }
    }
}
