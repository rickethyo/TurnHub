package com.turnhub.android.testing

import com.turnhub.android.protocol.AtlasInfo
import com.turnhub.android.protocol.AtlasWireParser
import com.turnhub.android.protocol.StateSnapshot
import org.json.JSONObject
import java.io.File

/**
 * Loads the shared contract fixtures in the repository's `protocol/examples/`
 * (the same files Atlas's host tests validate), so Android parses exactly what
 * the firmware contract promises rather than hand-written copies.
 */
object Fixtures {

    private val examples: File by lazy {
        generateSequence(File(System.getProperty("user.dir")!!).absoluteFile) { it.parentFile }
            .map { File(it, "protocol/examples") }
            .firstOrNull { File(it, "info.response.json").isFile }
            ?: error("protocol/examples not found above ${System.getProperty("user.dir")}")
    }

    fun text(name: String): String = File(examples, name).readText()

    fun json(name: String): JSONObject = JSONObject(text(name))

    fun info(edit: JSONObject.() -> Unit = {}): AtlasInfo =
        AtlasWireParser.parseInfo(json("info.response.json").apply(edit).toString())

    /** Parses a state fixture, optionally editing its JSON first. */
    fun state(name: String, edit: JSONObject.() -> Unit = {}): StateSnapshot =
        AtlasWireParser.parseState(json(name).apply(edit).toString())

    const val BOOT_ID = "0123456789ABCDEF0123456789ABCDEF"
    const val OTHER_BOOT_ID = "FEDCBA9876543210FEDCBA9876543210"
}
