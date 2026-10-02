package com.turnhub.android.data

import com.turnhub.android.protocol.AccountPermission
import com.turnhub.android.protocol.ProfileSummary
import com.turnhub.android.protocol.SetupStage
import com.turnhub.android.protocol.SetupStatus
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import org.json.JSONException
import org.json.JSONObject
import java.io.IOException

/** The app's first-run setup steps (Documentation/engineering/FIRST_RUN_SETUP.md). */
enum class SetupStep(val number: Int) {
    WELCOME(0),
    ACCOUNT(1),
    TABLE_CODE(2),
    SIGILS(3),
    UPDATES(4),
    WIFI(5),
    RESTARTING(5),
    DONE(6),
    ;

    companion object {
        /** Steps the progress label counts ("Step n of 5"). */
        const val COUNTED = 5
    }
}

/** One device's line in the update step while installing. */
data class UpdateProgress(val label: String, val state: String, val done: Boolean = false, val failed: Boolean = false)

/** The update step. */
sealed interface UpdatesState {
    data object NotChecked : UpdatesState
    data object Checking : UpdatesState

    /** No release is published yet, or the phone has no internet: setup goes on without one. */
    data class Unavailable(val reason: String) : UpdatesState

    /** [plan] lists every device; [UpdatePlan.anyUpdate] says whether Install is offered. */
    data class Ready(val release: String, val plan: UpdatePlan) : UpdatesState

    data class Installing(val lines: List<UpdateProgress>, val detail: String) : UpdatesState

    /** Finished; [lines] says how each device ended, in words. */
    data class Finished(val lines: List<UpdateProgress>) : UpdatesState
}

data class SetupState(
    /** Shown instead of the table while Atlas reports the Welcome stage. */
    val visible: Boolean = false,
    val step: SetupStep = SetupStep.WELCOME,
    val status: SetupStatus? = null,
    /** Saved accounts, for "I already have an account". */
    val profiles: List<ProfileSummary> = emptyList(),
    /** The Atlas screen is showing a six-digit code for this account. */
    val codeShowing: Boolean = false,
    val sigils: List<DeviceInfo> = emptyList(),
    val updates: UpdatesState = UpdatesState.NotChecked,
    val busy: Boolean = false,
    /** A problem, in words; the step stays where it is. */
    val error: String? = null,
    /** A plain status line (not an error). */
    val note: String? = null,
    /**
     * Only the update step, opened from the "Update available" banner after
     * setup ([AtlasSetupAssistant.openUpdates]); Close ends it.
     */
    val updatesOnly: Boolean = false,
)

/** What the assistant needs from the screen that owns the Atlas connection. */
interface SetupHost {
    /** The Atlas being set up, or null when disconnected. */
    fun endpoint(): AtlasEndpoint?

    /** Atlas's running firmware from `/api/v1/info`, e.g. "0.6.0-dev". */
    fun atlasFirmware(): String?

    /** The table's Wi-Fi now has this password: remember it for rejoining. */
    fun wifiPasswordChanged(ssid: String, password: String)

    /**
     * Atlas is restarting (update or new password). Rejoin its Wi-Fi and
     * reconnect; true once Atlas answers again (a new boot).
     */
    suspend fun reconnectAfterRestart(): Boolean
}

/**
 * Walks a phone through first-run setup: account, table code (which makes the
 * account the Admin), Sigil pairing, one update prompt for every device, and
 * the table's own Wi-Fi password. Atlas validates every step; this class only
 * sequences requests and says what happened.
 *
 * The PIN typed in the account step stays in memory for the whole setup:
 * Atlas sessions and table verification are RAM-only, so after an update
 * restarts Atlas the app signs in again by itself and asks for one new code.
 */
class AtlasSetupAssistant(
    private val session: AtlasPlayerSession,
    private val releases: FirmwareReleaseSource,
    private val host: SetupHost,
    /** How often Sigil update progress is read. */
    private val pollMs: Long = 2_000,
) {
    private val _state = MutableStateFlow(SetupState())
    val state: StateFlow<SetupState> = _state.asStateFlow()

    private var account: Pair<ProfileSummary, String>? = null
    private var sigilsPassed = false
    private var updatesPassed = false

    /** After a table code is confirmed, what to carry on with. */
    private var afterCode: (suspend () -> Unit)? = null

    // --- entry ------------------------------------------------------------------

    /** Reads Atlas's stage after connecting; shows setup only while it is Welcome. */
    suspend fun check() {
        val endpoint = host.endpoint() ?: return
        val status = try {
            session.setupStatus(endpoint)
        } catch (e: AtlasException) {
            return // The table view reports connection problems.
        }
        val current = _state.value
        when {
            status.stage == SetupStage.WELCOME && !current.visible -> {
                // A new run (first connect, or Atlas was factory reset): nothing from an earlier run counts.
                resetProgress()
                _state.value = SetupState(visible = true, status = status)
            }
            // Mid-setup but signed out of a table that is back at Welcome: Atlas was factory reset.
            status.stage == SetupStage.WELCOME && !current.busy && current.step.number >= SetupStep.TABLE_CODE.number &&
                current.step != SetupStep.RESTARTING && current.updates !is UpdatesState.Installing &&
                session.state.value !is PlayerSessionState.SignedIn -> {
                resetProgress()
                _state.value = SetupState(visible = true, status = status)
            }
            status.stage == SetupStage.WELCOME -> _state.update { it.copy(status = status) }
            // An update restarted Atlas: keep showing its progress.
            current.updatesOnly -> _state.update { it.copy(status = status) }
            current.visible && current.step == SetupStep.RESTARTING ->
                _state.update { it.copy(status = status, step = SetupStep.DONE, busy = false, error = null) }
            current.visible && current.step == SetupStep.DONE -> _state.update { it.copy(status = status) }
            else -> _state.value = SetupState(status = status)
        }
    }

    private fun resetProgress() {
        account = null
        sigilsPassed = false
        updatesPassed = false
        afterCode = null
    }

    /** "Not now": back to the table until the next connection. Atlas keeps showing Welcome. */
    fun dismiss() {
        _state.update { it.copy(visible = false) }
    }

    /** Welcome's Start, and every "Continue". */
    suspend fun next() {
        clearMessages()
        val signedIn = session.state.value as? PlayerSessionState.SignedIn
        val info = signedIn?.info
        val next = when {
            signedIn == null -> SetupStep.ACCOUNT
            info?.has(AccountPermission.ADMIN) != true || !verified() -> SetupStep.TABLE_CODE
            !sigilsPassed -> SetupStep.SIGILS
            !updatesPassed -> SetupStep.UPDATES
            else -> SetupStep.WIFI
        }
        _state.update { it.copy(step = next) }
        when (next) {
            SetupStep.ACCOUNT -> loadProfiles()
            // Atlas shows its code only when asked, so ask as the step opens: the screen and the app agree.
            SetupStep.TABLE_CODE -> if (!_state.value.codeShowing) requestCodeQuietly()
            SetupStep.SIGILS -> refreshSigils()
            SetupStep.UPDATES -> if (_state.value.updates == UpdatesState.NotChecked) checkUpdates()
            else -> Unit
        }
    }

    // --- 1. account -------------------------------------------------------------

    suspend fun createAccount(name: String, pin: String) {
        val endpoint = host.endpoint() ?: return
        val clean = name.trim()
        if (clean.isEmpty() || clean.length > 32) return fail("Choose a name of 1 to 32 characters.")
        if (!PIN.matches(pin)) return fail("PINs are 4 to 8 digits.")
        work {
            session.register(endpoint, clean, pin)
            val id = (session.state.value as? PlayerSessionState.SignedIn)?.profileId ?: return@work
            account = ProfileSummary(id, clean, hasPin = true) to pin
            next()
        }
    }

    suspend fun signIn(profile: ProfileSummary, pin: String) {
        val endpoint = host.endpoint() ?: return
        if (!PIN.matches(pin)) return fail("PINs are 4 to 8 digits.")
        work {
            session.signIn(endpoint, profile, pin)
            account = profile to pin
            next()
        }
    }

    // --- 2. table code ----------------------------------------------------------

    /** Asks Atlas to show a code for this account. */
    suspend fun requestCode() = work {
        val response = session.raw("POST", "/api/presence/request") ?: return@work signedOut()
        if (!response.ok) return@work fail(errorOf(response) ?: "Atlas could not show a code.")
        _state.update { it.copy(codeShowing = true, note = "Atlas is showing a code. Scan it or type it in.") }
    }

    /** The code from the Atlas screen; then this account becomes the Admin if there is none yet. */
    suspend fun confirmCode(code: String) = work {
        val digits = code.filter(Char::isDigit)
        if (digits.length != 6) return@work fail("The code has six digits.")
        val response = session.raw("POST", "/api/presence/confirm", listOf("code" to digits))
            ?: return@work signedOut()
        if (!response.ok) return@work fail(errorOf(response) ?: "That code was not accepted.")
        _state.update { it.copy(codeShowing = false) }
        val status = host.endpoint()?.let { session.setupStatus(it) }
        if (status != null && !status.adminExists) {
            val made = session.raw("POST", "/api/accounts/setup") ?: return@work signedOut()
            if (!made.ok) return@work fail(errorOf(made) ?: "Atlas could not make this account the Admin.")
        }
        session.refresh()
        val admin = (session.state.value as? PlayerSessionState.SignedIn)?.info?.has(AccountPermission.ADMIN) == true
        if (!admin) {
            return@work fail("This table already has an Admin. Sign in as that account to finish setup.")
        }
        val resume = afterCode
        afterCode = null
        if (resume != null) resume() else next()
    }

    // --- 3. Sigils --------------------------------------------------------------

    /** Re-reads the paired Sigils (the screen calls this every few seconds on this step). */
    suspend fun refreshSigils() {
        val response = try {
            session.raw("GET", "/api/devices")
        } catch (e: AtlasException) {
            null
        } ?: return
        if (!response.ok) return
        val list = parseDevices(response.body)
        _state.update { it.copy(sigils = list) }
    }

    suspend fun sigilsDone() {
        sigilsPassed = true
        next()
    }

    // --- 4. updates -------------------------------------------------------------

    suspend fun checkUpdates() {
        _state.update { it.copy(updates = UpdatesState.Checking, error = null) }
        refreshSigils()
        val feed = try {
            releases.latestFeed()
        } catch (e: CancellationException) {
            throw e
        } catch (e: NoReleaseException) {
            return setUpdates(UpdatesState.Unavailable("No firmware release has been published yet."))
        } catch (e: IOException) {
            return setUpdates(
                UpdatesState.Unavailable("Couldn't reach GitHub to check for updates. The phone needs internet for this."),
            )
        } catch (e: IllegalArgumentException) {
            return setUpdates(UpdatesState.Unavailable("The release list couldn't be read: ${e.message}"))
        }
        val firmware = host.atlasFirmware().orEmpty()
        setUpdates(UpdatesState.Ready(feed.release, UpdatePlan.build(feed, firmware, _state.value.sigils)))
    }

    fun skipUpdates() {
        updatesPassed = true
    }

    /**
     * The update step on its own, after setup (the "Update available" banner).
     * Installing takes an Admin who has confirmed a table code (Atlas
     * enforces both; the code is asked for when Install starts). An update
     * restarts Atlas, which forgets this phone's session, so Sigils left over
     * are installed by opening this again after signing in.
     */
    suspend fun openUpdates() {
        resetProgress()
        val status = _state.value.status
        val admin = (session.state.value as? PlayerSessionState.SignedIn)?.info?.has(AccountPermission.ADMIN) == true
        if (!admin) {
            _state.value = SetupState(
                visible = true, step = SetupStep.UPDATES, status = status, updatesOnly = true,
                updates = UpdatesState.Unavailable("Sign in as an Admin on the Account tab to install updates."),
            )
            return
        }
        _state.value = SetupState(visible = true, step = SetupStep.UPDATES, status = status, updatesOnly = true)
        checkUpdates()
    }

    /** Install every pending update: Atlas first, then each Sigil in turn. */
    suspend fun installUpdates() {
        val ready = _state.value.updates as? UpdatesState.Ready ?: return
        val pending = ready.plan.pending
        if (pending.isEmpty()) return
        val lines = pending.map { UpdateProgress(it.label, "Waiting") }.toMutableList()
        fun show(detail: String) = setUpdates(UpdatesState.Installing(lines.toList(), detail))
        fun mark(index: Int, text: String, done: Boolean = false, failed: Boolean = false) {
            lines[index] = lines[index].copy(state = text, done = done, failed = failed)
        }
        _state.update { it.copy(busy = true, error = null) }
        try {
            if (!ensureVerified()) {
                afterCode = { installUpdates() }
                return
            }
            val atlasIndex = pending.indexOfFirst { it.sigilId == null }
            if (atlasIndex >= 0 && lines[atlasIndex].state == "Waiting") {
                if (!updateAtlas(pending[atlasIndex], atlasIndex, ::mark, ::show)) return finishUpdates(lines)
                // Atlas restarted: sessions and verification are gone. Sign in
                // again, then one new code before the Sigils.
                if (pending.size > 1) {
                    show("Atlas is back. Enter the new Atlas code to update the Sigils.")
                    if (!reSignIn()) return finishUpdates(lines)
                    val rest = pending.drop(atlasIndex + 1)
                    afterCode = { installSigils(rest, lines, atlasIndex + 1) }
                    requestCodeQuietly()
                    return
                }
                return finishUpdates(lines)
            }
            installSigils(pending.filter { it.sigilId != null }, lines, 0)
        } catch (e: AtlasException) {
            // Atlas restarting or dropping the upload is expected here; never let it crash the app.
            abortUpdates(lines, e)
        } finally {
            _state.update { it.copy(busy = false) }
        }
    }

    // --- 5. Wi-Fi and finish ----------------------------------------------------

    /** Saves the table's own Wi-Fi password and finishes setup; Atlas restarts onto it. */
    suspend fun finish(password: String) {
        if (password.length !in 8..63) return fail("Wi-Fi passwords are 8 to 63 characters.")
        if (password == WifiCredentials.DEFAULT_ATLAS_PASSPHRASE) {
            return fail("Choose a password other than the one printed for setup.")
        }
        work {
            val response = session.raw("POST", "/api/setup/finish", listOf("password" to password))
                ?: return@work signedOut()
            if (response.code == 403 && presenceRequired(response)) {
                afterCode = { finish(password) }
                requestCodeQuietly()
                return@work
            }
            if (!response.ok) return@work fail(errorOf(response) ?: "Atlas didn't finish setup.")
            val ssid = _state.value.status?.ssid?.takeIf { it.isNotBlank() } ?: WifiCredentials.DEFAULT_ATLAS_SSID
            host.wifiPasswordChanged(ssid, password)
            _state.update { it.copy(step = SetupStep.RESTARTING, note = "Atlas is restarting with your new Wi-Fi password.") }
            if (host.reconnectAfterRestart()) {
                check()
            } else {
                fail("Atlas hasn't come back yet. Rejoin $ssid with your new password, then connect again.")
            }
        }
    }

    /** "You're all set" → the table. */
    fun close() {
        _state.value = SetupState(status = _state.value.status)
        resetProgress()
    }

    // --- update helpers ---------------------------------------------------------

    private suspend fun updateAtlas(
        target: UpdateTarget,
        index: Int,
        mark: (Int, String, Boolean, Boolean) -> Unit,
        show: (String) -> Unit,
    ): Boolean {
        val pkg = target.available ?: return false
        mark(index, "Downloading", false, false)
        show("Downloading Atlas ${pkg.version} from GitHub")
        val bytes = download(pkg) { mark(index, it, false, true); show(it) } ?: return false
        mark(index, "Installing", false, false)
        show("Sending the update to Atlas. Keep the phone near the table.")
        val response = session.upload("/api/firmware", "firmware", pkg.file, bytes)
        if (response == null || !response.ok) {
            val reason = response?.let(::errorOf) ?: "Atlas refused the update"
            mark(index, reason, false, true)
            show(reason)
            return false
        }
        mark(index, "Restarting", false, false)
        show("Atlas is restarting with ${pkg.version}.")
        if (!host.reconnectAfterRestart()) {
            mark(index, "Didn't come back yet", false, true)
            show("Atlas hasn't come back yet. Wait for its screen, then connect again.")
            return false
        }
        val running = FirmwareVersion.parse(host.atlasFirmware().orEmpty())
        return if (running != null && running >= pkg.version) {
            mark(index, "Updated to $running", true, false)
            true
        } else {
            mark(index, "Still on ${running ?: "the old version"}", false, true)
            false
        }
    }

    private suspend fun installSigils(targets: List<UpdateTarget>, lines: MutableList<UpdateProgress>, firstLine: Int) {
        _state.update { it.copy(busy = true) }
        try {
            fun show(detail: String) = setUpdates(UpdatesState.Installing(lines.toList(), detail))
            // One package is staged on Atlas at a time: group by display type.
            for ((product, group) in targets.groupBy { it.product }) {
                val pkg = group.first().available ?: continue
                val indexes = group.map { t -> firstLine + targets.indexOf(t) }
                show("Downloading the ${product.label} update")
                val bytes = download(pkg) { reason ->
                    indexes.forEach { lines[it] = lines[it].copy(state = reason, failed = true) }
                    show(reason)
                } ?: continue
                show("Sending the ${product.label} update to Atlas")
                val staged = session.upload("/api/sigil-firmware", "firmware", pkg.file, bytes)
                if (staged == null || !staged.ok) {
                    val reason = staged?.let(::errorOf) ?: "Atlas refused the package"
                    indexes.forEach { lines[it] = lines[it].copy(state = reason, failed = true) }
                    show(reason)
                    continue
                }
                for ((i, target) in group.withIndex()) {
                    val line = indexes[i]
                    if (!target.online) {
                        lines[line] = lines[line].copy(state = "Offline: turn it on and update later", failed = true)
                        continue
                    }
                    lines[line] = lines[line].copy(state = "Updating")
                    show("Updating ${target.label}. It restarts when done.")
                    lines[line] = updateOneSigil(target, pkg).let { (ok, text) ->
                        lines[line].copy(state = text, done = ok, failed = !ok)
                    }
                }
            }
            finishUpdates(lines)
        } catch (e: AtlasException) {
            abortUpdates(lines, e)
        } finally {
            _state.update { it.copy(busy = false) }
        }
    }

    /** An Atlas request failed mid-update: every unfinished device shows why, and the step ends. */
    private fun abortUpdates(lines: MutableList<UpdateProgress>, e: AtlasException) {
        val reason = e.failure.userMessage
        for (i in lines.indices) {
            if (!lines[i].done && !lines[i].failed) lines[i] = lines[i].copy(state = reason, failed = true)
        }
        finishUpdates(lines)
    }

    private suspend fun updateOneSigil(target: UpdateTarget, pkg: FirmwarePackage): Pair<Boolean, String> {
        val id = target.sigilId ?: return false to "Not a Sigil"
        val start = session.raw("POST", "/api/sigil-update", listOf("module" to id.toString()))
            ?: return false to "Signed out"
        if (!start.ok) return false to (errorOf(start) ?: "Atlas didn't start the update")
        // The Sigil downloads over Wi-Fi and restarts: about 20-40 s each.
        repeat(SIGIL_POLLS) {
            delay(pollMs)
            val status = session.raw("GET", "/api/sigil-firmware") ?: return false to "Signed out"
            if (!status.ok) return@repeat
            val json = try {
                JSONObject(status.body)
            } catch (_: JSONException) {
                return@repeat
            }
            when (json.optString("stage")) {
                "done" -> return true to "Updated to ${pkg.version}"
                "failed" -> return false to (json.optString("message").ifBlank { "The update failed" })
            }
        }
        return false to "No answer from the Sigil; check it and try again later"
    }

    private suspend fun download(pkg: FirmwarePackage, failed: (String) -> Unit): ByteArray? = try {
        releases.download(pkg)
    } catch (e: CancellationException) {
        throw e
    } catch (e: IOException) {
        failed("Download failed: ${e.message}")
        null
    }

    private fun finishUpdates(lines: List<UpdateProgress>) {
        updatesPassed = true
        setUpdates(UpdatesState.Finished(lines.toList()))
    }

    // --- plumbing ---------------------------------------------------------------

    private suspend fun verified(): Boolean {
        val response = try {
            session.raw("GET", "/api/presence")
        } catch (e: AtlasException) {
            null
        } ?: return false
        if (!response.ok) return false
        return try {
            JSONObject(response.body).optLong("remainingMs") > MIN_VERIFIED_MS
        } catch (_: JSONException) {
            false
        }
    }

    /** True if verified with time to spare; otherwise asks for a code and returns false. */
    private suspend fun ensureVerified(): Boolean {
        if (verified()) return true
        requestCodeQuietly()
        return false
    }

    private suspend fun requestCodeQuietly() {
        val response = session.raw("POST", "/api/presence/request") ?: return signedOut()
        if (!response.ok) return fail(errorOf(response) ?: "Atlas could not show a code.")
        _state.update { it.copy(codeShowing = true, note = "Atlas is showing a code. Scan it or type it in.") }
    }

    private suspend fun reSignIn(): Boolean {
        val (profile, pin) = account ?: return false.also { fail("Sign in again to continue.") }
        val endpoint = host.endpoint() ?: return false
        return try {
            session.signIn(endpoint, profile, pin)
            true
        } catch (e: AtlasException) {
            fail(e.failure.userMessage)
            false
        }
    }

    private suspend fun loadProfiles() {
        val endpoint = host.endpoint() ?: return
        val list = try {
            session.profiles(endpoint)
        } catch (e: AtlasException) {
            emptyList()
        }
        _state.update { it.copy(profiles = list.filter { it.hasPin }) }
    }

    /** Runs one step's requests: busy while running, Atlas failures become [SetupState.error]. */
    private suspend fun work(block: suspend () -> Unit) {
        _state.update { it.copy(busy = true, error = null) }
        try {
            block()
        } catch (e: CancellationException) {
            throw e
        } catch (e: AtlasException) {
            fail(e.failure.userMessage)
        } finally {
            _state.update { it.copy(busy = false) }
        }
    }

    private fun setUpdates(value: UpdatesState) = _state.update { it.copy(updates = value) }

    private fun clearMessages() = _state.update { it.copy(error = null, note = null) }

    private fun fail(message: String) = _state.update { it.copy(error = message) }

    private fun signedOut() = fail("You were signed out. Sign in again to continue.")

    private fun errorOf(response: RawResponse): String? = try {
        JSONObject(response.body).optString("error").takeIf { it.isNotBlank() }
    } catch (_: JSONException) {
        null
    }

    private fun presenceRequired(response: RawResponse): Boolean = try {
        JSONObject(response.body).optBoolean("presenceRequired")
    } catch (_: JSONException) {
        false
    }

    private fun parseDevices(body: String): List<DeviceInfo> = try {
        val list = JSONObject(body).optJSONArray("devices")
        if (list == null) {
            emptyList()
        } else {
            (0 until list.length()).mapNotNull { list.optJSONObject(it) }.map { x ->
                DeviceInfo(
                    id = x.optInt("id"),
                    label = x.optString("label"),
                    defaultLabel = x.optString("defaultLabel"),
                    customName = x.optString("customName"),
                    hardwareId = x.optString("hardwareId"),
                    online = x.optBoolean("online"),
                    firmware = x.optString("firmware"),
                    display = x.optString("display"),
                    sessionCount = x.optInt("sessionCount"),
                    capabilities = x.optInt("capabilities"),
                )
            }.filter { !it.isHarness }
        }
    } catch (_: JSONException) {
        emptyList()
    }

    private companion object {
        val PIN = Regex("^\\d{4,8}$")

        /** A code lasts 10 minutes; ask again below 2 so an update never runs out mid-way. */
        const val MIN_VERIFIED_MS = 120_000L

        /** 2 s polls for up to 4 minutes per Sigil (Atlas's own reboot timeout is 2). */
        const val SIGIL_POLLS = 120
    }
}
