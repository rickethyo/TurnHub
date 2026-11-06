package com.turnhub.android.standalone

import android.content.Context
import com.turnhub.android.data.RawResponse
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import com.turnhub.android.data.TableControls
import com.turnhub.android.data.TabletSeat
import com.turnhub.android.protocol.GameProfile
import com.turnhub.android.protocol.TableState
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import org.json.JSONArray
import org.json.JSONObject
import java.util.UUID
import kotlin.random.asKotlinRandom

/** Owns the [StandaloneTable] for the Activity, so rotation keeps it. */
class StandaloneViewModel(val table: StandaloneTable) : androidx.lifecycle.ViewModel() {
    /** Survives rotation, but deliberately not process recreation. */
    var entryInitialized = false
}

/** Android owns the active game and a separate, versioned local library. */
interface StandaloneStore {
    fun loadGame(): String?
    /** Read-only legacy import queue, retained without inferred identities. */
    fun loadRecords(): String?
    fun loadLibrary(): String?
    /** Production stores both keys in one preference transaction. */
    fun saveSnapshot(game: String, library: String): Boolean
    /** A random outcome must reach durable storage before it is revealed. */
    fun commitSnapshot(game: String, library: String): Boolean = saveSnapshot(game, library)
    /** Copies unreadable saved data aside before a fresh start; false if it could not be kept. */
    fun keepUnreadable(): Boolean = false
}

/** Holds one game in memory and saves nowhere: the Table tab's practice table. */
class MemoryStandaloneStore(game: StandaloneGame, library: LocalLibrary) : StandaloneStore {
    private var game = StandaloneTable.gameToJson(game).toString()
    private var library = library.toJson().toString()
    override fun loadGame(): String = game
    override fun loadRecords(): String? = null
    override fun loadLibrary(): String = library
    override fun saveSnapshot(game: String, library: String): Boolean {
        this.game = game
        this.library = library
        return true
    }
}

class PreferencesStandaloneStore(context: Context) : StandaloneStore {
    private val prefs = context.applicationContext.getSharedPreferences("turnhub_standalone", Context.MODE_PRIVATE)
    override fun loadGame(): String? = prefs.getString("game", null)
    override fun loadRecords(): String? = prefs.getString("records", null)
    override fun loadLibrary(): String? = prefs.getString("library", null)
    override fun saveSnapshot(game: String, library: String): Boolean = prefs.edit()
        .putString("game", game).putString("library", library).apply().let { true }

    override fun commitSnapshot(game: String, library: String): Boolean = prefs.edit()
        .putString("game", game).putString("library", library).commit()

    // Both keys go in one editor, so the swap is atomic; the old data stays under *_unreadable.
    override fun keepUnreadable(): Boolean = prefs.edit()
        .putString("game_unreadable", prefs.getString("game", null))
        .putString("library_unreadable", prefs.getString("library", null))
        .putString("records_unreadable", prefs.getString("records", null))
        .commit()
}

data class StandaloneState(
    val game: StandaloneGame = StandaloneGame(),
    val library: LocalLibrary = LocalLibrary(),
    /** A damaged/unsupported library is kept untouched rather than silently reset. */
    val storageProblem: String? = null,
)

/**
 * Runs the standalone tablet game: the tablet table screen's [TableControls]
 * apply to [StandaloneGame] here instead of going to Atlas. Every change is
 * saved, so closing the app keeps the game; each finished game is kept as a
 * immutable [GameRecord] in local history, independent of optional Atlas delivery.
 */
class StandaloneTable(
    private val store: StandaloneStore,
    private val wallClock: () -> Long = System::currentTimeMillis,
    private val newId: () -> String = { UUID.randomUUID().toString() },
    /** Finished games shorter than this wait for [saveShortGame] instead of joining history. */
    private val shortGameMs: Long = StandaloneGame.SHORT_GAME_MS,
    /** The planes and phenomena a digital planar deck is built from (downloaded once; empty until then). */
    private val planarPool: () -> List<PlanarCard> = { emptyList() },
    /** Shuffles the planar deck and rolls the planar die. */
    private val planarRandom: kotlin.random.Random = java.security.SecureRandom().asKotlinRandom(),
) : TableControls {
    private val _state = MutableStateFlow(load())
    val state: StateFlow<StandaloneState> = _state.asStateFlow()

    /** Named presets are committed before the UI reports success. Same-name replacement is explicit. */
    fun saveGameSetup(name: String, replace: Boolean = false): Boolean {
        val before = _state.value
        val clean = name.trim()
        if (before.game.state != TableState.LOBBY || before.game.players.isEmpty() ||
            clean.isBlank() || clean.length > SavedGameSetup.MAX_NAME) return false
        val existing = before.library.gameSetups.any { it.name.equals(clean, ignoreCase = true) }
        if (existing && !replace) return false
        val setups = before.library.gameSetups.filterNot { it.name.equals(clean, ignoreCase = true) } +
            SavedGameSetup(clean, freshSetup(before.game))
        return publish(before.copy(library = before.library.copy(gameSetups = setups)), durable = true)
    }

    fun loadGameSetup(name: String): Boolean {
        val before = _state.value
        if (before.game.state != TableState.LOBBY) return false
        val setup = before.library.gameSetups.firstOrNull { it.name == name } ?: return false
        // A deleted saved identity must not be silently recreated or credited as someone else.
        if (setup.game.players.any { p -> p.localId != null && before.library.players.none { it.localId == p.localId } }) return false
        return publish(before.copy(game = freshSetup(setup.game).copy(revision = before.game.revision + 1)), durable = true)
    }

    fun deleteGameSetup(name: String): Boolean {
        val before = _state.value
        val setups = before.library.gameSetups.filterNot { it.name == name }
        if (setups.size == before.library.gameSetups.size) return false
        return publish(before.copy(library = before.library.copy(gameSetups = setups)), durable = true)
    }

    fun tick() { if (_state.value.storageProblem == null) { maintainCopiedProfiles(); edit { it.applyTurnTimeout(wallClock()) } } }
    fun setTimerRules(rules: com.turnhub.android.domain.LocalTimerRules) { edit { it.setTimerRules(rules) } }

    private val deliveryMutex = Mutex()
    private val unlockedAccounts = java.util.Collections.synchronizedSet(mutableSetOf<String>())
    private var connectedCopyAtlas: String? = null

    /** Copies never unlock a profile or grant this device's Admin role. */
    fun receiveSharedProfiles(atlasId: String, copies: List<JSONObject>): Boolean {
        val before = _state.value
        val incoming = runCatching { copies.map { SharedProfileCopy.decode(it, newId()) } }.getOrNull() ?: return false
        if (incoming.map { it.profileId }.distinct().size != incoming.size) return false
        var players = before.library.players
        val relock = mutableListOf<String>()
        for (copy in incoming) {
            val old = players.firstOrNull { it.profileId == copy.profileId }
            if (old == null) players = players + copy.copy(copiedFromAtlas = atlasId)
            else if (copy.lastActivityMs > old.lastActivityMs || (old.credential == null && copy.credential != null && copy.lastActivityMs == old.lastActivityMs)) {
                val credential = if (before.library.isDeviceAdmin(old.localId) && copy.credential == null) old.credential else copy.credential
                if (old.credential != credential) relock += old.localId
                val merged = old.copy(name = copy.name, credential = credential,
                    lastActivityMs = copy.lastActivityMs,
                    createdAtMs = old.createdAtMs.takeIf { it > 0 } ?: copy.createdAtMs,
                    personalization = old.personalization.copy(color = copy.personalization.color,
                        avatar = copy.personalization.avatar, reduceMotion = copy.personalization.reduceMotion))
                players = players.map { if (it.localId == old.localId) merged else it }
            }
        }
        if (players == before.library.players) return true
        val saved = publish(before.copy(library = before.library.copy(players = players)), durable = true)
        if (saved) unlockedAccounts.removeAll(relock.toSet())
        return saved
    }

    fun keepCopiedProfiles(keep: Boolean): Boolean {
        val before = _state.value
        if (before.library.deviceAdmin?.let { !accountUnlocked(it) } == true) return false
        return publish(before.copy(library = before.library.copy(keepCopiedProfiles = keep)), durable = true)
    }

    /** A persisted 24-hour grace period; never removes a current or saved setup's player. */
    fun observeCopyConnection(atlasId: String?) {
        connectedCopyAtlas = atlasId
        maintainCopiedProfiles()
    }

    internal fun maintainCopiedProfiles() {
        val before = _state.value
        if (before.storageProblem != null) return
        val library = before.library
        val now = wallClock()
        val sources = library.players.mapNotNull { it.copiedFromAtlas }.toSet()
        val disconnected = library.atlasDisconnectedAtMs.filterKeys { it in sources && it != connectedCopyAtlas }.toMutableMap()
        sources.filter { it != connectedCopyAtlas }.forEach { disconnected.putIfAbsent(it, now) }
        val inUse = (before.game.players + library.gameSetups.flatMap { it.game.players }).mapNotNull { it.localId }.toSet()
        val players = if (library.keepCopiedProfiles) library.players else library.players.filter { player ->
            val since = disconnected[player.copiedFromAtlas]
            since == null || now - since < 24 * 60 * 60 * 1000L ||
                player.localId in inUse || player.localId == library.deviceAdmin
        }
        val next = library.copy(players = players, atlasDisconnectedAtMs = disconnected)
        if (next != library) publish(before.copy(library = next), durable = true)
    }
    fun accountUnlocked(localId: String): Boolean = _state.value.library.players.firstOrNull { it.localId == localId }
        ?.let { it.credential == null || localId in unlockedAccounts } == true
    fun lockAccount(localId: String) { unlockedAccounts.remove(localId) }

    fun unlockAccount(localId: String, secret: String): Boolean {
        val player = _state.value.library.players.firstOrNull { it.localId == localId } ?: return false
        if (player.credential?.verifies(secret) == false) return false
        unlockedAccounts += localId
        return true
    }
    fun setAccountCredential(localId: String, credential: LocalCredential): Boolean {
        if (!accountUnlocked(localId)) return false
        val before = _state.value
        val saved = publish(before.copy(library = before.library.copy(players = before.library.players.map {
            if (it.localId == localId) it.copy(credential = credential, lastActivityMs = maxOf(wallClock(),it.lastActivityMs+1)) else it })))
        // Whoever just chose the PIN stays signed in to the account, as after createAccount.
        if (saved) unlockedAccounts += localId
        return saved
    }

    /** Removes the account's PIN or password, so its settings open without one. */
    fun clearAccountCredential(localId: String): Boolean {
        if (!accountUnlocked(localId)) return false
        val before = _state.value
        if (before.library.players.none { it.localId == localId && it.credential != null }) return false
        // The device's Admin account keeps its PIN or password.
        if (before.library.isDeviceAdmin(localId)) return false
        return publish(before.copy(library = before.library.copy(players = before.library.players.map {
            if (it.localId == localId) it.copy(credential = null, lastActivityMs = maxOf(wallClock(),it.lastActivityMs+1)) else it })))
    }

    /**
     * Deletes a saved account from this phone. Finished games stay in history
     * under the name played with; a game in progress keeps the account until it
     * goes back to the lobby.
     */
    fun deleteAccount(localId: String): Boolean {
        if (!accountUnlocked(localId)) return false
        val before = _state.value
        if (before.library.isDeviceAdmin(localId) || before.library.players.none { it.localId == localId }) return false
        val game = before.game
        if (game.state != TableState.LOBBY && game.players.any { it.localId == localId }) return false
        var lobby = game
        lobby.players.indexOfFirst { it.localId == localId }.takeIf { it >= 0 }?.let { lobby = lobby.removePlayer(it) }
        val library = before.library.copy(players = before.library.players.filterNot { it.localId == localId })
        val saved = publish(before.copy(game = lobby, library = library))
        if (saved) unlockedAccounts -= localId
        return saved
    }

    /**
     * Makes [localId] this device's Admin account (owner, 2026-10-29): the owner's account type,
     * with no extra powers on a phone. It needs a PIN or password and must be unlocked; handing
     * Admin on also needs the current Admin unlocked.
     */
    fun setDeviceAdmin(localId: String): Boolean {
        val before = _state.value
        val current = before.library.deviceAdmin
        if (current == localId) return true
        if (before.library.players.none { it.localId == localId && it.credential != null } || !accountUnlocked(localId) ||
            (current != null && !accountUnlocked(current))) return false
        return publish(before.copy(library = before.library.copy(deviceAdmin = localId)), durable = true)
    }

    /** First-time setup: a new account with its PIN or password, made the device's Admin. */
    fun createAdminAccount(name: String, credential: LocalCredential): Boolean {
        val before = _state.value
        val clean = name.trim().take(StandaloneGame.MAX_NAME)
        if (before.library.deviceAdmin != null || clean.isBlank()) return false
        var player = SavedLocalPlayer(newId(), clean, credential = credential, createdAtMs = wallClock())
        while (before.library.players.any { it.profileId == player.profileId }) player = player.copy(profileId = LocalProfileIds.create())
        val saved = publish(before.copy(library = before.library.copy(players = before.library.players + player,
            deviceAdmin = player.localId)), durable = true)
        if (saved) unlockedAccounts += player.localId
        return saved
    }

    /** Explicit owner choice: verify the existing credential or give an unprotected account one. */
    fun promoteToDeviceAdmin(localId: String, secret: String?, newCredential: LocalCredential? = null): Boolean {
        val before = _state.value
        if (before.library.deviceAdmin != null) return false
        val player = before.library.players.firstOrNull { it.localId == localId } ?: return false
        val credential = player.credential?.also { if (secret == null || !it.verifies(secret)) return false }
            ?: newCredential ?: return false
        val saved = publish(before.copy(library = before.library.copy(deviceAdmin = localId,
            players = before.library.players.map { if (it.localId == localId) it.copy(credential = credential) else it })), durable = true)
        if (saved) unlockedAccounts += localId
        return saved
    }

    /** Only called after explicit Atlas setup authenticated this account; ordinary copies never grant Admin. */
    fun installAtlasSetupAdmin(profileId: String, name: String, secret: String): Boolean {
        val before = _state.value
        if (before.library.deviceAdmin != null) return true
        if (!LocalProfileIds.valid(profileId) || name.isBlank() || name.length > StandaloneGame.MAX_NAME ||
            !com.turnhub.android.data.ProfileSecret.isValid(secret)) return false
        val existing = before.library.players.firstOrNull { it.profileId == profileId }
        if (existing != null) return promoteToDeviceAdmin(existing.localId, secret,
            if (existing.credential == null) LocalCredential.create(secret) else null)
        val player = SavedLocalPlayer(newId(), name.trim(), profileId, credential = LocalCredential.create(secret), createdAtMs = wallClock())
        val saved = publish(before.copy(library = before.library.copy(players = before.library.players + player,
            deviceAdmin = player.localId)), durable = true)
        if (saved) unlockedAccounts += player.localId
        return saved
    }

    enum class CopyResult { ADDED, ALREADY_HERE, NOT_SAVED }

    /** All selected accounts and history reach durable storage together; the running game is untouched. */
    fun importPreviewProfiles(bundle: JSONObject): CopyResult {
        val before = _state.value
        if (before.storageProblem != null) return CopyResult.NOT_SAVED
        val merged = runCatching {
            val accounts = PreviewProfileTransfer.accounts(bundle)
            val staged = StandaloneTable(MemoryStandaloneStore(before.game, before.library))
            val admin = bundle.optStringOrNull("adminProfileId")
            for (account in accounts) {
                require(staged.addCopiedAccount(account, admin = account.getString("profileId") == admin) != CopyResult.NOT_SAVED)
            }
            staged.state.value.library
        }.getOrNull() ?: return CopyResult.NOT_SAVED
        if (merged == before.library) return CopyResult.ALREADY_HERE
        return if (publish(before.copy(library = merged), durable = true)) CopyResult.ADDED else CopyResult.NOT_SAVED
    }

    fun accountCopy(localId: String): JSONObject {
        val state = _state.value
        val player = requireNotNull(state.library.players.firstOrNull { it.localId == localId })
        require(accountUnlocked(localId)) { "Unlock the profile before sharing it" }
        return AccountCopy.toJson(player, state.library.history)
    }

    /**
     * Adds an account copied from another of the owner's devices ([AccountCopy]) under a new
     * local ID and the same profile ID; [admin] makes it this device's Admin when there is none.
     * The caller has already checked its PIN or password, so the account opens unlocked.
     */
    fun addCopiedAccount(copy: org.json.JSONObject, admin: Boolean): CopyResult {
        val before = _state.value
        val player = runCatching { AccountCopy.fromJson(copy, newId()) }.getOrNull() ?: return CopyResult.NOT_SAVED
        val existing = before.library.players.firstOrNull { it.profileId == player.profileId }
        if (existing != null && existing.credential != player.credential && !accountUnlocked(existing.localId)) return CopyResult.NOT_SAVED
        val target = existing ?: player
        val records = runCatching { AccountCopy.history(copy, target) }.getOrNull() ?: return CopyResult.NOT_SAVED
        var library = before.library
        for (record in records) {
            val old = library.history.firstOrNull { it.recordId == record.recordId }
            if (old == null) library = library.withRecord(record)
            else {
                // A copy can add this identity's binding, never replace the shared match outcome.
                val sameMatch = old.copy(players = old.players.map { it.copy(localId = null, profileId = null) }).toJson().toString() ==
                    record.copy(players = record.players.map { it.copy(localId = null, profileId = null) }).toJson().toString()
                if (!sameMatch) return CopyResult.NOT_SAVED
                library = library.copy(history = library.history.map { r -> if (r.recordId != old.recordId) r else
                    r.copy(players = r.players.mapIndexed { index, seat ->
                        if (record.players[index].localId == target.localId) seat.copy(localId = target.localId) else seat }) })
            }
        }
        if (existing != null) {
            val originalDate = listOf(existing.createdAtMs, player.createdAtMs).filter { it > 0 }.minOrNull() ?: 0
            library = library.copy(players = library.players.map { if (it.localId == existing.localId) it.copy(createdAtMs = originalDate) else it })
            if (admin && before.library.deviceAdmin == null && existing.credential != null)
                library = library.copy(deviceAdmin = existing.localId)
            if (library == before.library) return CopyResult.ALREADY_HERE
            val saved = publish(before.copy(library = library), durable = true)
            if (saved) unlockedAccounts += existing.localId
            return if (saved) CopyResult.ADDED else CopyResult.NOT_SAVED
        }
        // Another local account may already hold one of its Atlas links; this device keeps that one.
        val links = player.links.filterNot { l -> before.library.players.any { p -> p.links.any { it.atlasId == l.atlasId && it.profileId == l.profileId } } }
        val added = player.copy(links = links)
        val makeAdmin = admin && added.credential != null && before.library.deviceAdmin == null
        val saved = publish(before.copy(library = library.copy(players = before.library.players + added,
            deviceAdmin = if (makeAdmin) added.localId else before.library.deviceAdmin)), durable = true)
        if (!saved) return CopyResult.NOT_SAVED
        unlockedAccounts += added.localId
        return CopyResult.ADDED
    }

    // --- the lobby ------------------------------------------------------------------

    /** A quick seat without creating a persistent profile or asking for a name. */
    fun addGuest() {
        val names = _state.value.game.players.map { it.name }.toSet()
        val number = generateSequence(1) { it + 1 }.first { "Guest $it" !in names }
        addGuest("Guest $number")
    }

    /** Forget a saved guest choice; existing seats and match history keep their names. */
    fun forgetGuest(name: String): Boolean {
        val before = _state.value
        if (name !in before.library.guestNames) return false
        return publish(before.copy(library = before.library.copy(guestNames = before.library.guestNames - name)))
    }

    fun addGuest(name: String) {
        val clean = name.trim().take(StandaloneGame.MAX_NAME)
        val before = _state.value
        val game = before.game.addPlayer(clean, guest = true)
        if (game === before.game) return
        publish(before.copy(game = game, library = before.library.copy(
            guestNames = (before.library.guestNames + clean).distinct())))
    }

    /** Typing a name creates an identity; selecting a saved player reuses it. */
    fun addPlayer(name: String) {
        val clean = name.trim().take(StandaloneGame.MAX_NAME)
        val before = _state.value
        if (clean.isEmpty() || before.game.state != TableState.LOBBY ||
            before.game.players.size >= StandaloneGame.MAX_PLAYERS) return
        var player = SavedLocalPlayer(newId(), clean, createdAtMs = wallClock())
        while (before.library.players.any { it.profileId == player.profileId } || !LocalProfileIds.valid(player.profileId)) {
            player = player.copy(profileId = LocalProfileIds.create())
        }
        val game = before.game.addPlayer(clean, localId = player.localId)
        publish(before.copy(game = game, library = before.library.copy(players = before.library.players + player)))
    }
    /** Public room directory; credentials and history never leave through a snapshot. */
    internal fun roomProfiles() = org.json.JSONArray(_state.value.library.players.map { p ->
        org.json.JSONObject().put("profileId", p.profileId).put("name", p.name).put("hasPin", p.credential != null)
            .put("color", p.personalization.color ?: org.json.JSONObject.NULL).put("avatar", p.personalization.avatar)
    })

    /** A paired Atlas controller may play an existing identity, never unlock its private data. */
    internal fun attachAtlasRoomProfile(request: JSONObject): String? {
        val id=request.optString("profileId")
        val player=_state.value.library.players.firstOrNull { it.profileId==id } ?: return null
        val game=_state.value.game
        if(game.players.none { it.localId==player.localId }) {
            if(game.state!=TableState.LOBBY) return null
            val added=game.addPlayer(player.name,localId=player.localId)
            if(added===game || !publishGame(added,durable=true)) return null
        }
        return id
    }

    /** A first room link proves its PIN; subsequent connections use the room credential. */
    internal fun attachRoomProfile(request: org.json.JSONObject): String? {
        val before = _state.value
        val offered = request.optJSONObject("profile")
        if (offered != null && offered.toString().length > 128 * 1024) return null
        val id = request.optString("profileId").ifBlank { offered?.optString("profileId").orEmpty() }
        if (!LocalProfileIds.valid(id)) return null
        var player = before.library.players.firstOrNull { it.profileId == id }
        val secret = request.optString("pin")
        if (player != null) {
            if (player.credential?.verifies(secret) == false) return null
        } else {
            if (offered == null || offered.optString("profileId") != id) return null
            val name = offered.optString("name").trim()
            if (name.isBlank() || name.length > StandaloneGame.MAX_NAME) return null
            val credential = offered.optJSONObject("credential")?.let {
                LocalCredential(it.getString("salt"), it.getString("hash"))
            }
            if (credential != null && (credential.salt.length != 24 || credential.hash.length != 44 || !credential.verifies(secret))) return null
            val look = offered.optJSONObject("look")?.let { LocalPersonalization.fromJson(it) } ?: LocalPersonalization()
            player = SavedLocalPlayer(newId(), name, id, look, credential = credential,
                createdAtMs = offered.optLong("createdAtMs").coerceAtLeast(0))
        }
        var resolved = player ?: return null
        if (offered?.optString("profileId") == id && offered.optLong("activity") > resolved.lastActivityMs) {
            val name = offered.optString("name").trim()
            val look = offered.optJSONObject("look")?.let { LocalPersonalization.fromJson(it) } ?: resolved.personalization
            if (name.isNotBlank() && name.length <= StandaloneGame.MAX_NAME) resolved = resolved.copy(name = name, personalization = look,
                lastActivityMs = offered.optLong("activity"))
        }
        val original = offered?.optLong("createdAtMs") ?: 0
        if (original > 0 && (resolved.createdAtMs == 0L || original < resolved.createdAtMs)) resolved = resolved.copy(createdAtMs = original)
        request.put("_profile", org.json.JSONObject().put("profileId", id).put("name", resolved.name)
            .put("look", resolved.personalization.toJson()).put("activity", resolved.lastActivityMs).put("createdAtMs", resolved.createdAtMs))
        val existing = before.library.players.any { it.profileId == id }
        val library = before.library.copy(players = if (existing) before.library.players.map { if (it.profileId == id) resolved else it } else before.library.players + resolved)
        val renamed = before.game.players.any { it.localId == resolved.localId && it.name != resolved.name }
        val current = if (renamed) before.game.copy(players = before.game.players.map {
            if (it.localId == resolved.localId) it.copy(name = resolved.name) else it }, revision = before.game.revision + 1) else before.game
        val game = if (current.state == TableState.LOBBY && current.players.none { it.localId == resolved.localId })
            current.addPlayer(resolved.name, localId = resolved.localId) else current
        if (!publish(before.copy(game = game, library = library), durable = true)) { request.put("_attachError", "unavailable"); return null }
        return id
    }

    /** Games of [profileId]'s account here, by record ID ([HistorySync]). */
    internal fun historyIds(profileId: String): List<String> = HistorySync.ids(_state.value.library, profileId)

    /** Up to one page of [profileId]'s games, as they leave this device. */
    internal fun historyRecords(profileId: String, ids: Collection<String>): org.json.JSONArray =
        HistorySync.export(_state.value.library, profileId, ids)

    /** Saves another device's copies of [profileId]'s games; null when nothing could be saved. */
    internal fun mergeHistory(profileId: String, records: org.json.JSONArray): HistorySync.Merge? {
        val before = _state.value
        if (before.storageProblem != null) return null
        val merge = runCatching { HistorySync.merge(before.library, profileId, records) }.getOrNull() ?: return null
        if (merge.library == before.library) return merge
        return merge.takeIf { publish(before.copy(library = merge.library), durable = true) }
    }

    /**
     * The room's history exchange for a joined member whose identity is [profileId]
     * (plans/profile-sync.md): `index` lists the games here, `pull` sends asked-for games,
     * `push` merges the guest's. Null answers "unavailable".
     */
    internal fun roomHistory(profileId: String, request: org.json.JSONObject): org.json.JSONObject? {
        if (!LocalProfileIds.valid(profileId) || _state.value.library.players.none { it.profileId == profileId }) return null
        val answer = org.json.JSONObject()
        when (request.optString("step")) {
            "index" -> answer.put("records", org.json.JSONArray(historyIds(profileId).takeLast(HistorySync.MAX_INDEX)))
            "pull" -> {
                val ids = request.getJSONArray("ids")
                require(ids.length() <= HistorySync.PAGE)
                answer.put("history", historyRecords(profileId, (0 until ids.length()).map { ids.getString(it) }))
            }
            "push" -> {
                val records = request.getJSONArray("history")
                require(records.length() <= HistorySync.PAGE)
                val merge = mergeHistory(profileId, records) ?: return null
                answer.put("added", merge.added + merge.bound).put("conflicts", merge.conflicts)
            }
            else -> throw IllegalArgumentException("Unknown history step")
        }
        return answer
    }

    fun selectPlayer(localId: String) {
        val player = _state.value.library.players.firstOrNull { it.localId == localId } ?: return
        edit { it.addPlayer(player.name, localId = player.localId) }
    }
    fun createAccount(name: String, credential: LocalCredential? = null): Boolean {
        val clean = name.trim().take(StandaloneGame.MAX_NAME)
        if (clean.isBlank()) return false
        val before = _state.value
        var player = SavedLocalPlayer(newId(), clean, createdAtMs = wallClock())
        player = player.copy(credential = credential)
        while (before.library.players.any { it.profileId == player.profileId } || !LocalProfileIds.valid(player.profileId)) {
            player = player.copy(profileId = LocalProfileIds.create())
        }
        val saved = publish(before.copy(library = before.library.copy(players = before.library.players + player)))
        if (saved) unlockedAccounts += player.localId
        return saved
    }

    fun saveAccount(localId: String, name: String, personalization: LocalPersonalization): Boolean {
        if (!accountUnlocked(localId)) return false
        val before = _state.value
        if (name.isBlank() || name.length > StandaloneGame.MAX_NAME || before.library.players.none { it.localId == localId }) return false
        if (runCatching { LocalPersonalization.fromJson(personalization.toJson()) }.isFailure) return false
        val library = before.library.copy(players = before.library.players.map {
            if (it.localId != localId) it
            else it.copy(name = name.trim(), personalization = personalization,
                // A change to what syncs with an Atlas makes this copy the newest.
                lastActivityMs = if (it.name != name.trim() || it.personalization.syncedPart() != personalization.syncedPart())
                    wallClock() else it.lastActivityMs)
        })
        // Historical names remain snapshots. Active labels are presentation, so rename immediately.
        val game = before.game.copy(players = before.game.players.map {
            if (it.localId == localId) it.copy(name = name.trim()) else it }, revision = before.game.revision + 1)
        return publish(before.copy(game = game, library = library))
    }

    /** Copy only public gameplay identity. No secret, privileged role or private history is imported. */
    fun referenceProfile(atlasId: String, profileId: String, name: String): String? {
        val before = _state.value
        before.library.players.firstOrNull { it.profileId == profileId ||
            it.links.any { link -> link.atlasId == atlasId && link.profileId == profileId } }?.let { return it.localId }
        val clean = name.trim().take(StandaloneGame.MAX_NAME)
        if (atlasId.isBlank() || clean.isBlank() || !LocalProfileIds.valid(profileId)) return null
        val player = SavedLocalPlayer(newId(), clean, profileId,
            links = listOf(AtlasProfileLink(atlasId, profileId)))
        return if (publish(before.copy(library = before.library.copy(players = before.library.players + player)))) player.localId else null
    }

    /** Called only after Atlas creation or credential verification; never from a name match. */
    /**
     * Brings an Atlas account back onto this device (a fresh install or a new phone): same
     * profile ID, Atlas's name and look, linked with [linkKey] so profile sync takes over.
     * [credential] is the PIN or password just checked by Atlas. Refused if it's already here.
     */
    fun restoreAccount(atlasId: String, profileId: String, name: String, look: LocalPersonalization,
                       linkKey: String?, credential: LocalCredential): Boolean {
        val before = _state.value
        val clean = name.trim().take(StandaloneGame.MAX_NAME)
        if (atlasId.isBlank() || clean.isBlank() || !LocalProfileIds.valid(profileId) ||
            (linkKey != null && !linkKey.matches(Regex("[0-9A-F]{32}"))) ||
            before.library.players.any { it.profileId == profileId || it.links.any { l -> l.atlasId == atlasId && l.profileId == profileId } })
            return false
        // Never edited here (0), and already matching Atlas's copy, so the first sync changes nothing.
        val player = SavedLocalPlayer(newId(), clean, profileId, look.copy(image = null, thumbnail = null),
            listOf(AtlasProfileLink(atlasId, profileId, linkKey)), credential)
        val saved = publish(before.copy(library = before.library.copy(players = before.library.players + player)))
        if (saved) unlockedAccounts += player.localId
        return saved
    }

    /**
     * Games an Atlas kept for a linked account (`/api/profiles/history`), added to this device's
     * history as already delivered there, so they count here and are never sent back. Players
     * linked to this Atlas become their local accounts; a record already here is skipped.
     * Returns how many were added.
     */
    fun addAtlasRecords(atlasId: String, records: List<org.json.JSONObject>): Int {
        val before = _state.value
        val local = before.library.players.flatMap { p ->
            p.links.filter { it.atlasId == atlasId }.map { it.profileId to p } + (p.profileId to p)
        }.toMap()
        var library = before.library
        var added = 0
        for (json in records) {
            val record = GameRecord.fromJson(json) ?: continue
            if (library.history.any { it.recordId == record.recordId }) continue
            val atlasIds = record.players.map { it.profileId.orEmpty() }
            val mine = record.copy(players = record.players.map { p ->
                local[p.profileId]?.let { p.copy(localId = it.localId, profileId = it.profileId) } ?: p.copy(localId = null)
            })
            library = library.copy(history = library.history + mine)
                .withDelivery(MatchDelivery(mine.recordId, DeliveryStatus.IMPORTED, atlasId = atlasId, profileIds = atlasIds))
            added++
        }
        if (added > 0 && !publish(before.copy(library = library))) return 0
        return added
    }

    fun linkAccount(localId: String, atlasId: String, profileId: String, linkKey: String? = null): Boolean {
        val before = _state.value
        if (atlasId.isBlank() || !LocalProfileIds.valid(profileId) || before.library.players.none { it.localId == localId } ||
            (linkKey != null && !linkKey.matches(Regex("[0-9A-F]{32}"))) ||
            before.library.players.any { it.localId != localId && it.links.any { l -> l.atlasId == atlasId && l.profileId == profileId } }) return false
        return publish(before.copy(library = before.library.copy(players = before.library.players.map {
            if (it.localId != localId) it else {
                // Relinking the same profile keeps its sync key unless a new one came.
                val old = it.links.firstOrNull { l -> l.atlasId == atlasId && l.profileId == profileId }
                it.copy(links = it.links.filterNot { l -> l.atlasId == atlasId } + AtlasProfileLink(atlasId, profileId, linkKey ?: old?.linkKey))
            } })))
    }

    /**
     * Takes an Atlas's newer copy of a linked profile's synced settings
     * (plans: profile-transfer F) without counting it as a new local change.
     * [avatar] null keeps the local look (Atlas has an uploaded image).
     */
    fun applySyncedProfile(localId: String, name: String, color: String?, avatar: String?, reduceMotion: Boolean, activityMs: Long): Boolean {
        val before = _state.value
        val player = before.library.players.firstOrNull { it.localId == localId } ?: return false
        val clean = name.trim()
        if (clean.isBlank() || clean.length > StandaloneGame.MAX_NAME || (color != null && !color.matches(Regex("#[0-9a-fA-F]{6}"))) ||
            (avatar != null && avatar !in LocalPersonalization.AVATARS)) return false
        val look = player.personalization.let { p ->
            p.copy(color = color, reduceMotion = reduceMotion).let { if (avatar == null) it else it.copy(avatar = avatar, image = null, thumbnail = null) }
        }
        if (clean == player.name && look == player.personalization && activityMs == player.lastActivityMs) return true
        val library = before.library.copy(players = before.library.players.map {
            if (it.localId == localId) it.copy(name = clean, personalization = look, lastActivityMs = activityMs) else it })
        val game = before.game.copy(players = before.game.players.map {
            if (it.localId == localId) it.copy(name = clean) else it }, revision = before.game.revision + 1)
        return publish(before.copy(game = game, library = library))
    }

    /** Forgets a sync key Atlas no longer accepts; the link itself stays. */
    fun dropLinkKey(localId: String, atlasId: String): Boolean {
        val before = _state.value
        return publish(before.copy(library = before.library.copy(players = before.library.players.map {
            if (it.localId != localId) it else it.copy(links = it.links.map { l -> if (l.atlasId == atlasId) l.copy(linkKey = null) else l })
        })))
    }

    fun keepProfileCreationTime(localId: String, originalDate: Long): Boolean {
        if (originalDate <= 0) return false
        val before = _state.value
        val player = before.library.players.firstOrNull { it.localId == localId } ?: return false
        if (player.createdAtMs > 0 && player.createdAtMs <= originalDate) return false
        return publish(before.copy(library = before.library.copy(players = before.library.players.map {
            if (it.localId == localId) it.copy(createdAtMs = originalDate) else it
        })))
    }

    /**
     * Queues every match whose players are linked to this Atlas. With [guests],
     * a match needs one linked player, and a match already sent here with guests
     * is sent again when another of its players has since been linked. Confirmed
     * player-copy exchanges also retry rejected deliveries using exact identities.
     */
    fun queueAutomaticImports(atlasId: String, available: Set<String>, guests: Boolean = false, copies: Boolean = false) {
        _state.value.library.history.forEach { record ->
            val delivery = _state.value.library.delivery(record.recordId)
            val again = guests && delivery.status == DeliveryStatus.IMPORTED && delivery.atlasId == atlasId && delivery.hasGuests
            if (delivery.status == DeliveryStatus.NEEDS_LINKING || copies && delivery.status == DeliveryStatus.REJECTED || again) {
                _state.value.library.automaticMapping(record, atlasId, available, guests, copies)
                    ?.let { queueImport(record.recordId, atlasId, it, available, guests) }
            }
        }
    }
    fun removePlayer(index: Int) = edit { it.removePlayer(index) }
    fun chooseStarter(index: Int?) = edit { it.chooseStarter(index) }

    /** The finished game, when it was too short to count on its own and hasn't been saved. */
    fun unsavedShortGame(): GameRecord? = _state.value.let { s ->
        s.game.record()?.takeIf { !it.counted(shortGameMs) && s.library.history.none { r -> r.recordId == it.recordId } }
    }

    /** Counts a short finished game after all: it joins history and stats like any other. */
    fun saveShortGame(): Boolean {
        val record = unsavedShortGame() ?: return false
        return publish(_state.value.copy(library = _state.value.library.withRecord(record)))
    }
    fun clearPlayers() = edit { it.clearPlayers() }
    fun movePlayer(index: Int, by: Int) = edit { it.movePlayer(index, by) }
    fun swapPlayers(a: Int, b: Int) = edit { it.swapPlayers(a, b) }
    fun setFormat(profile: GameProfile) = edit { it.setFormat(profile) }
    fun setStartingLife(life: Int) = edit { it.setStartingLife(life) }
    fun setTurnTimer(ms: Long) = edit { it.setTurnTimer(ms) }
    fun setTwoHeadedGiant(on: Boolean) = edit { it.setTwoHeadedGiant(on) }
    fun setHouseRules(on: Boolean) = edit { it.setHouseRules(on) }
    /** One settings update from the shared lobby, preserving unspecified fields. */
    fun configure(profile: GameProfile?, life: Int?, teams: Boolean?) = edit { game ->
        var next = profile?.let { game.setFormat(it) } ?: game
        teams?.let { next = next.setTwoHeadedGiant(it) }
        life?.let { next = next.setStartingLife(it) }
        next
    }
    fun start() = edit { if (planarSelectionReady(it)) it.copy(askEliminationSource = true).start(wallClock(), newId(), planarDeck = planarDeck(it)) else it }

    // --- Planechase -----------------------------------------------------------------

    /** A freshly shuffled digital deck for [game]; empty with physical cards or nothing downloaded. */
    private fun planarDeck(game: StandaloneGame): List<PlanarCard> =
        if (!game.planechase.enabled || game.planechase.source != PlanarDeckSource.DIGITAL) emptyList()
        else Planechase.shuffledDeck(game.planechase.selection.pool(planarPool()), game.players.size, game.planechase.phenomena, planarRandom)

    private fun planarSelectionReady(game: StandaloneGame): Boolean = !game.planechase.enabled ||
        game.planechase.source == PlanarDeckSource.PHYSICAL || game.planechase.selection.available(planarPool())

    fun setPlanarSelection(selection: PlanarSelection): Boolean {
        val before = _state.value
        if (before.game.state != TableState.LOBBY || !selection.valid() || !selection.available(planarPool())) return false
        return publish(before.copy(game = before.game.setPlanarSelection(selection)))
    }

    fun savePlanarDeck(selection: PlanarSelection): Boolean {
        val before = _state.value
        if (before.game.state != TableState.LOBBY || selection.cardIds == null || !selection.valid() ||
            !selection.available(planarPool())) return false
        val decks = before.library.planarDecks.filterNot { it.name == selection.name } + selection
        if (decks.size > 20) return false
        return publish(before.copy(game = before.game.setPlanarSelection(selection), library = before.library.copy(planarDecks = decks)))
    }

    fun deletePlanarDeck(name: String): Boolean {
        val before = _state.value
        if (before.game.state != TableState.LOBBY) return false
        return publish(before.copy(library = before.library.copy(planarDecks = before.library.planarDecks.filterNot { it.name == name })))
    }

    fun setPlanechase(on: Boolean) = edit { it.setPlanechase(on) }
    fun setTurnOrderReversed(on: Boolean) = edit { it.setTurnOrderReversed(on) }
    /** A digital game that started before the planes were downloaded switches to TurnHub's deck. */
    fun dealPlanarDeck() = edit { if (planarSelectionReady(it)) it.dealPlanarDeck(planarDeck(it)) else it }
    fun setPlanarSource(source: PlanarDeckSource) = edit { it.setPlanarSource(source) }
    fun setPlanarPhenomena(on: Boolean) = edit { it.setPlanarPhenomena(on) }

    // The Planes tab is one shared screen: it acts for the player whose turn it is.
    fun rollPlanarDie(fromEffect: Boolean = false) =
        edit { g -> g.active?.let { g.rollPlanarDie(it, PlanarFace.roll(planarRandom), fromEffect) } ?: g }
    fun planeswalk() = edit { g -> g.active?.let { g.planeswalk(it) } ?: g }
    fun resolvePhenomenon() = edit { g -> g.active?.let { g.resolvePhenomenon(it) } ?: g }
    fun chaosEnsues() = edit { g -> g.active?.let { g.chaosEnsues(it) } ?: g }
    fun planarTopToBottom() = edit { g -> g.active?.let { g.planarTopToBottom(it) } ?: g }
    fun changePlaneCounter(card: String, delta: Int) = edit { g -> g.active?.let { g.changePlaneCounter(it, card, delta) } ?: g }

    /**
     * A pending/acknowledged payload cannot be silently redirected or remapped.
     * With [guests], blank IDs are guests, and a match imported here with guests
     * may be queued again only to add players (everyone credited stays as is).
     */
    fun queueImport(recordId: String, atlasId: String, profileIds: List<String>, availableProfileIds: Set<String>,
                    guests: Boolean = false): Boolean {
        val before = _state.value
        val record = before.library.history.firstOrNull { it.recordId == recordId } ?: return false
        // Atlas can't import a format it doesn't run (PokÃ©mon): those games stay on this device.
        if (!record.profile.atlasSupported) return false
        val mapped = profileIds.filter { it.isNotBlank() }
        if (atlasId.isBlank() || profileIds.size != record.players.size || mapped.isEmpty() ||
            (!guests && mapped.size != profileIds.size) || mapped.any { it !in availableProfileIds } ||
            mapped.distinct().size != mapped.size) return false
        val old = before.library.delivery(recordId)
        if (old.status == DeliveryStatus.IMPORTED) {
            val adds = guests && old.atlasId == atlasId && old.profileIds.size == profileIds.size &&
                old.profileIds.indices.all { old.profileIds[it].isBlank() || old.profileIds[it] == profileIds[it] } &&
                mapped.size > old.profileIds.count { it.isNotBlank() }
            if (!adds) return false
        }
        if (old.status == DeliveryStatus.PENDING) return old.atlasId == atlasId && old.profileIds == profileIds
        return publish(before.copy(library = before.library.withDelivery(
            MatchDelivery(recordId, DeliveryStatus.PENDING, atlasId = atlasId, profileIds = profileIds.toList()))))
    }

    /** Gives a waiting (not yet acknowledged) match back to linking, e.g. for another Atlas. */
    fun cancelImport(recordId: String): Boolean {
        val before = _state.value
        val old = before.library.delivery(recordId)
        if (old.status != DeliveryStatus.PENDING) return false
        return publish(before.copy(library = before.library.withDelivery(MatchDelivery(recordId))))
    }

    /** Sends only explicitly mapped work for this Atlas, serialized and oldest first. */
    suspend fun sendRecords(atlasId: String, post: suspend (List<Pair<String, String>>) -> RawResponse?): Int =
        deliveryMutex.withLock {
            var taken = 0
            for (record in _state.value.library.history) {
                val delivery = _state.value.library.delivery(record.recordId)
                if (delivery.status != DeliveryStatus.PENDING || delivery.atlasId != atlasId) continue
                if (_state.value.storageProblem != null) break
                val response = post(record.importFields(atlasId, delivery.profileIds))
                val next = when {
                    response == null -> delivery.copy(reason = "No acknowledgement from Atlas. Retry this same mapping after reconnecting.")
                    response.code == 400 -> delivery.copy(status = DeliveryStatus.REJECTED,
                        reason = response.reason("Atlas rejected this record (HTTP 400). Review the player mapping."))
                    response.ok -> {
                        val detail = runCatching {
                            val ack = JSONObject(response.body)
                            require(ack.getBoolean("ok"))
                            val duplicate = ack.getBoolean("duplicate")
                            val credited = ack.getInt("credited")
                            require(credited in 0..record.players.size)
                            val unmatched = ack.getJSONArray("unmatched")
                            val names = (0 until unmatched.length()).map { unmatched.getString(it) }
                            val guests = delivery.profileIds.count { it.isBlank() }
                            if (duplicate) "Atlas already had this match."
                            else "Atlas credited $credited of ${record.players.size} players." +
                                (if (guests == 0) "" else " $guests not linked yet; they're added when linked.") +
                                if (names.isEmpty()) "" else " Not credited: ${names.joinToString()}."
                        }.getOrNull()
                        if (detail != null) delivery.copy(status = DeliveryStatus.IMPORTED, reason = detail)
                        else delivery.copy(reason = "Atlas returned no valid import acknowledgement. Retry the same mapping.")
                    }
                    else -> delivery.copy(reason = response.reason("Atlas import is waiting (HTTP ${response.code}). Retry after reconnecting."))
                }
                if (!publish(_state.value.copy(library = _state.value.library.withDelivery(next)))) break
                if (next.status == DeliveryStatus.IMPORTED) taken++
                // Rejected records stay visible but do not block other queued work.
                // A lost acknowledgement blocks newer work so retries stay inside
                // Atlas's bounded receipt window rather than evicting uncertain IDs.
                if (next.status == DeliveryStatus.PENDING) break
            }
            taken
        }

    private fun RawResponse.reason(fallback: String): String = runCatching {
        JSONObject(body).optString("error").takeIf { it.isNotBlank() }?.take(300)
    }.getOrNull() ?: fallback

    // --- the table ------------------------------------------------------------------

    override suspend fun resolveElimination(seat: TabletSeat, victim: Int, source: Int?, token: com.turnhub.android.protocol.GameActionToken): Boolean {
        val before = _state.value.game
        if (before.gameId != token.bootId || before.revision != token.revision ||
            StandaloneGame.indexOf(seat.moduleId) !in before.players.indices) return false
        edit { it.resolveElimination(victim, source?.takeIf { n -> n > 0 }?.minus(1), source == 0, wallClock()) }
        return _state.value.game !== before
    }

    override suspend fun doom(seat: TabletSeat, action: com.turnhub.android.protocol.DoomAction,
        token: com.turnhub.android.protocol.GameActionToken, player: Int, value: Int): Boolean {
        val before = _state.value.game
        val index = StandaloneGame.indexOf(seat.moduleId)
        if (before.gameId != token.bootId || before.revision != token.revision ||
            before.players.getOrNull(index)?.eliminated != false) return false
        edit { game -> when (action) {
            com.turnhub.android.protocol.DoomAction.DRAW -> game.drawDoom(token.cycle, wallClock(), planarRandom)
            com.turnhub.android.protocol.DoomAction.ELIGIBILITY -> if (value in 0..1) game.doomEligibility(player, value == 1, token.cycle) else game
            com.turnhub.android.protocol.DoomAction.CREDIT -> game.doomCredit(player, value, token.cycle)
        } }
        return _state.value.game !== before
    }

    override suspend fun eliminate(seat: TabletSeat, eliminatedBy: Int?): Boolean {
        val before = _state.value.game
        if (eliminatedBy != null && eliminatedBy !in 0..before.players.size) return false
        edit { it.eliminate(StandaloneGame.indexOf(seat.moduleId), wallClock(), eliminatedBy = eliminatedBy?.takeIf { it > 0 }?.minus(1), self = eliminatedBy == 0) }
        return _state.value.game !== before
    }

    override suspend fun control(seat: TabletSeat, action: String): Boolean {
        val index = StandaloneGame.indexOf(seat.moduleId)
        val now = wallClock()
        val before = _state.value.game
        edit { game ->
            when (action) {
                "pass" -> game.pass(index, now)
                "extend-turn" -> game.extendTurn(index, now)
                "pause" -> game.togglePause(now)
                "concede" -> game.concede(index, now)
                "eliminate" -> game.eliminate(index, now)
                "monarch" -> game.claimMonarch(index)
                "day" -> game.setDayNight(com.turnhub.android.domain.DayNight.DAY)
                "night" -> game.setDayNight(com.turnhub.android.domain.DayNight.NIGHT)
                "turn-order-reversed" -> game.setTurnOrderReversed(true)
                "turn-order-clockwise" -> game.setTurnOrderReversed(false)
                "day-night-clear" -> game.setDayNight(com.turnhub.android.domain.DayNight.NEITHER)
                "win" -> game.claimWin(index, now)
                // A player's own view (the host's single-player view) asks the others first.
                "claim-win" -> game.requestWin(index, now)
                "confirm" -> game.answerWinClaim(index, true, now)
                "deny" -> game.answerWinClaim(index, false, now)
                "draw" -> game.endWithoutWinner(now)
                "rematch" -> if (planarSelectionReady(game)) game.rematch(now, newId(), planarDeck = planarDeck(game)) else game
                "sean-mode" -> if (planarSelectionReady(game)) game.seanModeRematch(now, newId(), planarDeck = planarDeck(game)) else game
                "reset" -> game.reset()
                "back-to-setup" -> game.backToSetup(now)
                "checkup-done" -> game.dismissCheckup()
                // "everyone:rad:1": the game menu's one tap for every player's counter.
                else -> if (action.startsWith(StandaloneGame.EVERYONE_PREFIX)) {
                    val (kind, delta) = action.removePrefix(StandaloneGame.EVERYONE_PREFIX).split(':').let { it.getOrNull(0) to it.getOrNull(1)?.toIntOrNull() }
                    if (kind != null && delta != null) game.changeCounterForEveryone(kind, delta, now) else game
                } else game
            }
        }
        return _state.value.game !== before
    }

    override suspend fun life(seat: TabletSeat, delta: Int): Boolean {
        val before = _state.value.game
        edit { it.changeLife(StandaloneGame.indexOf(seat.moduleId), delta, wallClock()) }
        return _state.value.game !== before
    }

    override suspend fun commander(seat: TabletSeat, source: Int, commander: Int, delta: Int) =
        edit { it.commanderDamage(StandaloneGame.indexOf(seat.moduleId), source - 1, commander, delta, wallClock()) }

    // A standalone game has no phones asking for life changes, and is never offline.
    fun setCommander(index: Int, name: String?, art: String? = null, cardOnly: Boolean = false) {
        val before = _state.value
        val game = before.game.setCommander(index, name, art, cardOnly)
        val player = game.players.getOrNull(index) ?: return
        val library = if (player.localId != null && !player.cardOnly && player.commander != null) {
            val choice = player.commander to player.commanderArt
            before.library.copy(players = before.library.players.map { saved ->
                if (saved.localId != player.localId) saved else saved.copy(
                    commanders = (listOf(choice) + saved.commanders.filterNot { it == choice }).take(6))
            })
        } else before.library
        publish(before.copy(game = game, library = library), durable = true)
    }
    fun setZeroLifeElimination(enabled: Boolean) { edit { it.setAutoEliminateAtZeroLife(enabled, wallClock()) } }

    override suspend fun counter(seat: TabletSeat, kind: String, value: Int, commander: Int) =
        edit { it.changeCounter(StandaloneGame.indexOf(seat.moduleId), kind, value, commander, wallClock()) }

    override suspend fun setEliminationRule(seat: TabletSeat, field: String, value: Int) = changeEliminationRule(field, value)

    /** The whole table's elimination rules; the Game tab's game settings change them too. */
    fun changeEliminationRule(field: String, value: Int) = edit { it.setEliminationRule(field, value, wallClock()) }

    override suspend fun setAutoEliminateAtZeroLife(seat: TabletSeat, enabled: Boolean) {
        setZeroLifeElimination(enabled)
    }

    override suspend fun respondLife(seat: TabletSeat, requestId: Long, accept: Boolean) = Unit
    override suspend fun disable() = Unit

    // --- plumbing -------------------------------------------------------------------

    /** LAN capabilities already select the seat. Freshness and mode are rechecked here. */
    /**
     * Resumes a phone-hosted game Atlas kept when its last hub went away
     * (protocol/hosted-v1.md): only while this phone has no game under way, so
     * it never replaces one. The game's players stay as that hub named them.
     */
    fun resumeHostedGame(json: JSONObject): Boolean {
        val current = _state.value.game
        val underway = setOf(TableState.STARTING, TableState.RUNNING, TableState.PAUSED)
        if (_state.value.storageProblem != null || current.state in underway) return false
        val game = runCatching { gameFromJson(json) }.getOrNull() ?: return false
        if (game.gameId.isBlank() || game.state !in underway) return false
        return publishGame(game, durable = true)
    }

    /**
     * Phone-hosted games (protocol/hosted-v1.md): an input from the Atlas this phone hosts on.
     * Atlas only names the seat; this phone's rules decide. Player 0 is a table action:
     * a Sigil attaches or detaches in setup (bound by sigilSeat "sigil:<id>:<slot>"), or starts the game.
     */
    internal fun applyHostedInput(player: Int, action: JSONObject): Boolean {
        val game = _state.value.game
        if (_state.value.storageProblem != null) return false
        if (player == 0) {
            val sigil = action.optInt("sigil", -1)
            val slot = action.optInt("slot", -1)
            if (game.state != TableState.LOBBY) return false
            return when (action.optString("kind")) {
                "sigil-join" -> {
                    if (sigil !in 0..7 || slot !in 1..2) return false
                    val binding = "sigil:$sigil:$slot"
                    val previous = action.optInt("previousPlayer", -1)
                    if (previous >= 0 && game.players.getOrNull(previous)?.sigilSeat != binding) return false
                    if (previous < 0 && game.players.any { it.sigilSeat == binding || it.localId == binding }) return false
                    val available = if (previous >= 0) game.copy(players = game.players.mapIndexed { index, p ->
                        if(index == previous) p.copy(sigilSeat = null) else p
                    }) else game
                    val target = action.optString("target")
                    val profile = action.optString("profileId")
                    val mode = action.optString("mode", "guest")
                    var library = _state.value.library
                    val next = when (mode) {
                        "attach" -> {
                            // Match the identity snapshot, never a mutable player number or name.
                            val index = action.optInt("playerIndex", -1)
                            val candidate = game.players.getOrNull(index) ?: return false
                            if (candidate.localId.orEmpty() != target || (_state.value.library.players.firstOrNull { it.localId == candidate.localId }?.profileId ?: candidate.profileId).orEmpty() != profile ||
                                candidate.name != action.optString("name")) return false
                            if (index < 0 || game.players[index].sigilSeat != null) return false
                            available.copy(players = available.players.mapIndexed { i, p -> if (i == index) p.copy(sigilSeat = binding) else p },
                                revision = game.revision + 1)
                        }
                        "profile" -> {
                            if (!LocalProfileIds.valid(profile)) return false
                            var saved = library.players.firstOrNull { it.profileId == profile }
                            if (saved == null) {
                                val name = action.optString("name").trim().take(StandaloneGame.MAX_NAME)
                                if (name.isEmpty()) return false
                                saved = SavedLocalPlayer(newId(), name, profile)
                                library = library.copy(players = library.players + saved)
                            }
                            if (game.players.any { it.localId == saved.localId || it.profileId == profile }) return false
                            val added = available.addPlayer(saved.name, profileId = profile, localId = saved.localId)
                            if (added === available) return false
                            added.copy(players = added.players.dropLast(1) + added.players.last().copy(sigilSeat = binding))
                        }
                        "guest" -> {
                            val name = action.optString("name").trim().take(StandaloneGame.MAX_NAME)
                                .ifBlank { "Sigil ${sigil + 1}" + if (slot == 2) " B" else "" }
                            val added = available.addPlayer(name, guest = true)
                            if (added === available) return false
                            added.copy(players = added.players.dropLast(1) + added.players.last().copy(sigilSeat = binding))
                        }
                        else -> return false
                    }
                    publish(_state.value.copy(game = next, library = library))
                }
                "sigil-leave" -> {
                    if (sigil !in 0..7 || slot !in 0..2) return false
                    val prefix = "sigil:$sigil:"
                    val next = game.copy(players = game.players.map { p ->
                        if (p.sigilSeat?.let { if (slot == 0) it.startsWith(prefix) else it == "$prefix$slot" } == true)
                            p.copy(sigilSeat = null) else p
                    }, revision = game.revision + 1)
                    // Leaving releases this controller; the player and other phones stay seated.
                    next.players != game.players && publishGame(next, durable = true)
                }
                "start" -> { if (game.players.size < 2) return false; start(); _state.value.game.state != TableState.LOBBY }
                else -> false
            }
        }
        return applyLanAction(com.turnhub.android.lan.LanAction(game.gameId, game.revision, player,
            action.optString("kind"), action.optInt("value"), action.optInt("slot"), action.optInt("source")))
    }

    internal fun applyLanAction(action: com.turnhub.android.lan.LanAction): Boolean {
        val game = _state.value.game
        val planar = action.kind.startsWith(com.turnhub.android.lan.PLANAR_KIND)
        if (_state.value.storageProblem != null || game.gameId != action.match ||
            game.revision != action.revision || action.player !in (if (planar) 0 else 1)..game.players.size ||
            game.state !in listOf(TableState.LOBBY, TableState.RUNNING, TableState.PAUSED, TableState.GAME_OVER)) return false
        val simple = action.value == 0 && action.slot == 0 && action.source == 0
        if (simple && game.state == TableState.GAME_OVER) return when (action.kind) {
            "rematch" -> if (planarSelectionReady(game)) publishGame(game.rematch(wallClock(), newId(), planarDeck = planarDeck(game)), durable = true) else false
            "reset" -> publishGame(game.reset(), durable = true)
            else -> false
        }
        if (action.kind == "set-commander" && simple && game.state != TableState.GAME_OVER) {
            val card = action.card ?: return false
            setCommander(action.player - 1, card.name, card.art, card.cardOnly)
            return _state.value.game.revision != game.revision
        }
        if (game.state !in listOf(TableState.RUNNING, TableState.PAUSED)) return false
        if (planar) return applyPlanarLanAction(game, action)
        val index = action.player - 1
        return applyTcgLanAction(game, index, action)
    }

    /**
     * Any seated guest may pause or resume the table from their own phone. [value] 1 pauses and
     * 0 resumes, so a retried or crossed command never undoes another player's tap.
     */
    private fun lanPause(game: StandaloneGame, value: Int, now: Long): StandaloneGame =
        if ((game.state == TableState.RUNNING) == (value == 1)) game.togglePause(now) else game

    /**
     * The Planes tab's buttons from a guest: like the host's own tab they act for the active
     * player, whichever seat (or the planes display, player 0) sent them. The host rolls the die.
     */
    private fun applyPlanarLanAction(game: StandaloneGame, action: com.turnhub.android.lan.LanAction): Boolean {
        val index = game.active
        if (!game.profile.magicRules || !game.planechase.enabled || index == null || action.source != 0) return false
        if (action.player > 0 && game.players[action.player - 1].eliminated) return false
        val simple = action.value == 0 && action.slot == 0
        val next = when (action.kind) {
            "planar-roll" -> if (action.slot == 0 && action.value in 0..1)
                game.rollPlanarDie(if (action.player > 0) action.player - 1 else index,
                    PlanarFace.roll(planarRandom), fromEffect = action.value == 1) else game
            "planar-walk" -> if (simple) game.planeswalk(index) else game
            "planar-resolve" -> if (simple) game.resolvePhenomenon(index) else game
            "planar-chaos" -> if (simple) game.chaosEnsues(index) else game
            "planar-reverse" -> if (action.slot == 0 && action.value in 0..1) game.setTurnOrderReversed(action.value == 1) else game
            "planar-bottom" -> if (simple) game.planarTopToBottom(index) else game
            // Slot 0 is the table's own physical plane, otherwise the face-up card's position from 1.
            "planar-counter" -> if (action.value !in listOf(-1, 1)) game else {
                val card = if (action.slot == 0) Planechase.PHYSICAL_PLANE else game.planechase.faceUp.getOrNull(action.slot - 1)?.id
                card?.let { game.changePlaneCounter(index, it, action.value) } ?: game
            }
            else -> game
        }
        return publishGame(next, durable = true)
    }

    /** Own-seat capability adapter; all gameplay transitions remain in StandaloneGame. */
    private fun applyTcgLanAction(game: StandaloneGame, index: Int, action: com.turnhub.android.lan.LanAction): Boolean {
        if (!(game.profile.magicRules || game.profile.pokemon || game.profile.lorcana ||
                game.profile in setOf(GameProfile.YUGIOH, GameProfile.GENERIC))) return false
        val now = wallClock()
        val simple = action.value == 0 && action.slot == 0 && action.source == 0
        val everyone = com.turnhub.android.lan.everyoneCounterAction(action.kind, action.value, action.slot, action.source)
        val next = when {
            everyone != null && game.profile.magicRules && !game.players[index].eliminated ->
                game.changeCounterForEveryone(everyone.first, everyone.second, now)
            action.kind == "life" && action.slot == 0 && action.source == 0 && action.value != 0 && action.value in -10_000..10_000 ->
                game.changeLife(index, action.value, now)
            action.kind == "commander" && action.slot in 1..2 && action.source in 1..game.players.size && action.value in listOf(-1, 1) ->
                game.commanderDamage(index, action.source - 1, action.slot, action.value, now)
            simple && action.kind in setOf("pass", "extend-turn", "win", "confirm", "deny", "concede", "eliminate", "monarch") -> when (action.kind) {
                "pass" -> game.pass(index, now)
                "extend-turn" -> game.extendTurn(index, now)
                // A guest's claim waits on the other players, as a Sigil's does (owner, 2026-10-20).
                "win" -> game.requestWin(index, now)
                "confirm" -> game.answerWinClaim(index, true, now)
                "deny" -> game.answerWinClaim(index, false, now)
                "concede" -> game.concede(index, now)
                "eliminate" -> game.eliminate(index, now)
                "monarch" -> game.claimMonarch(index)
                else -> game
            }
            action.kind == "pause" && action.slot == 0 && action.source == 0 && action.value in 0..1 ->
                lanPause(game, action.value, now)
            action.kind == StandaloneGame.RAD_KIND && action.slot == 0 && action.source == 0 && action.value in 0..StandaloneGame.MAX_LIFE ->
                game.changeCounter(index, action.kind, action.value, nowMs = now)
            action.source == 0 && action.slot in 0..2 && action.kind in setOf("poison", "energy", "experience", "treasure", "rad",
                "commander-casts", "city-blessing", "initiative") && action.value in -1..1 ->
                game.changeCounter(index, action.kind, action.value, action.slot.coerceAtLeast(1), now)
            action.source == 0 && action.slot == 0 && action.value in 0..1 &&
                (action.kind.startsWith(StandaloneGame.ACTION_KIND) || action.kind.startsWith(StandaloneGame.CONDITION_KIND)) ->
                game.changeCounter(index, action.kind, action.value, nowMs = now)
            else -> game
        }
        return publishGame(next, durable = true)
    }

    private fun edit(change: (StandaloneGame) -> StandaloneGame) {
        publishGame(change(_state.value.game))
    }

    private fun publishGame(after: StandaloneGame, durable: Boolean = false): Boolean {
        val before = _state.value.game
        if (after === before) return false
        val record = after.record()?.takeIf { (before.state != TableState.GAME_OVER || before.gameId != after.gameId) && it.counted(shortGameMs) }
        val library = record?.let { _state.value.library.withRecord(it) } ?: _state.value.library
        return publish(_state.value.copy(game = after, library = library), durable || record != null)
    }

    /** Leaves a blocked store: keeps the unreadable data aside, then starts empty. */
    fun startFresh(): Boolean {
        if (_state.value.storageProblem == null || !store.keepUnreadable()) return false
        val fresh = StandaloneState()
        val saved = runCatching { store.saveSnapshot(gameToJson(fresh.game).toString(), fresh.library.toJson().toString()) }
            .getOrDefault(false)
        if (saved) _state.value = fresh
        return saved
    }

    private fun publish(next: StandaloneState, durable: Boolean = false): Boolean {
        if (_state.value.storageProblem != null) return false
        val saved = runCatching {
            val gameJson = gameToJson(next.game).toString()
            val libraryJson = next.library.toJson().toString()
            if (durable || next.game.doomBag.lastCheck > _state.value.game.doomBag.lastCheck)
                store.commitSnapshot(gameJson, libraryJson)
            else store.saveSnapshot(gameJson, libraryJson)
        }
            .getOrDefault(false)
        if (!saved) {
            _state.update { it.copy(storageProblem = "Couldn't save local play. Close and reopen the app before continuing; do not clear app data.") }
            return false
        }
        _state.value = next
        return true
    }

    private fun load(): StandaloneState {
        return try {
            val game = store.loadGame()?.let { gameFromJson(JSONObject(it)) } ?: StandaloneGame()
            val savedLibrary = store.loadLibrary()
            var library = if (savedLibrary != null) LocalLibrary.fromJson(JSONObject(savedLibrary)) else {
                // Preserve the old queue as history, without inventing identity from its labels.
                val records = store.loadRecords()?.let { JSONArray(it) } ?: JSONArray()
                LocalLibrary(history = (0 until records.length()).map { i ->
                    requireNotNull(GameRecord.fromJson(records.getJSONObject(i)))
                })
            }
            // Also covers a game that was completed before its old queue write reached storage.
            game.record()?.takeIf { it.counted(shortGameMs) }?.let { library = library.withRecord(it) }
            if (savedLibrary != null && JSONObject(savedLibrary).getInt("schema") == 1) {
                check(store.saveSnapshot(gameToJson(game).toString(), library.toJson().toString())) { "Could not save upgraded accounts" }
            }
            StandaloneState(game = game, library = library)
        } catch (_: Exception) {
            StandaloneState(storageProblem = "Local saved data could not be read. It has been kept untouched. Do not clear app data; recovery needs review.")
        }
    }

    companion object {
        fun gameToJson(game: StandaloneGame): JSONObject = JSONObject().apply {
            put("gameId", game.gameId)
            put("state", game.state.name)
            put("gameProfile", game.profile.wireValue)
            put("startingLife", game.startingLife)
            put("autoEliminateAtZeroLife", game.autoEliminateAtZeroLife)
            put("askEliminationSource", game.askEliminationSource)
            put("active", game.active ?: JSONObject.NULL)
            put("starter", game.starter ?: JSONObject.NULL)
            put("winner", game.winner ?: JSONObject.NULL)
            put("winners", JSONArray(game.winners))
            put("monarch", game.monarch ?: JSONObject.NULL)
            put("initiative", game.initiative ?: JSONObject.NULL)
            put("dayNight", game.dayNight.name)
            put("turnOrderReversed", game.turnOrderReversed)
            game.winClaim?.let { put("winClaim", it); put("winConfirmed", JSONArray(game.winConfirmed.sorted())) }
            put("autoEliminateAtPoisonLimit", game.autoEliminateAtPoisonLimit)
            put("autoEliminateAtCommanderDamage", game.autoEliminateAtCommanderDamage)
            put("poisonLimit", game.poisonLimit)
            put("turnTimerMs", game.turnTimerMs)
            put("timerRules", JSONObject().put("timeout", game.timerRules.timeout.name)
                .put("earnExtensions", game.timerRules.earnExtensions)
                .put("earnBeforePercent", game.timerRules.earnBeforePercent)
                .put("maxEarnedPerGame", game.timerRules.maxEarnedPerGame))
            put("turnExtensionMs", game.turnExtensionMs)
            put("timeoutHandled", game.timeoutHandled)
            put("twoHeadedGiant", game.twoHeadedGiant)
            put("houseRules", game.houseRules)
            put("startedAtMs", game.startedAtMs)
            put("elapsedMs", game.elapsedMs)
            put("runningSinceMs", game.runningSinceMs ?: JSONObject.NULL)
            put("turnStartedAtElapsedMs", game.turnStartedAtElapsedMs)
            put("revision", game.revision)
            put("planechase", game.planechase.toJson())
            put("doomBag", game.doomBag.toSavedJson())
            put("pokemonCheckup", JSONArray(game.pokemonCheckup))
            put("radDue", JSONArray(game.radDue))
            put("players", JSONArray().apply {
                game.players.forEach { p ->
                    put(JSONObject().apply {
                        put("name", p.name)
                        put("profileId", p.profileId ?: JSONObject.NULL)
                        put("localId", p.localId ?: JSONObject.NULL)
                        put("sigilSeat", p.sigilSeat ?: JSONObject.NULL)
                        put("guest", p.guest)
                        put("life", p.life)
                        put("eliminated", p.eliminated)
                        put("conceded", p.conceded)
                        put("eliminatedBy", p.eliminatedBy ?: JSONObject.NULL)
                        put("selfEliminated", p.selfEliminated)
                        put("eliminationPending", p.eliminationPending)
                        put("counters", JSONObject().apply {
                            put("poison", p.counters.poison); put("energy", p.counters.energy); put("experience", p.counters.experience)
                            put("commanderCasts", JSONArray(p.counters.commanderCasts)); put("cityBlessing", p.counters.cityBlessing)
                            put("treasure", p.counters.treasure); put("rad", p.counters.rad)
                        })
                        put("outOrder", p.outOrder ?: JSONObject.NULL)
                        put("turnsCompleted", p.turnsCompleted)
                        put("demerits", p.demerits)
                        put("extensionsEarned", p.extensionsEarned)
                        put("extensionsUsed", p.extensionsUsed)
                        put("turnMs", p.turnMs)
                        put("fastestTurnMs", p.fastestTurnMs)
                        put("longestTurnMs", p.longestTurnMs)
                        put("commander", p.commander ?: JSONObject.NULL)
                        put("commanderArt", p.commanderArt ?: JSONObject.NULL)
                        put("cardOnly", p.cardOnly)
                        put("turn", JSONObject()
                            .put("actions", JSONArray(p.turn.turnActions.map { it.name }))
                            .put("conditions", JSONArray(p.turn.conditions.map { it.name })))
                        put("commanderDamage", JSONObject().apply {
                            p.commanderDamage.forEach { (source, damage) -> put("$source", JSONArray(damage)) }
                        })
                    })
                }
            })
        }

        fun gameFromJson(json: JSONObject): StandaloneGame {
            val players = json.getJSONArray("players")
            return StandaloneGame(
                gameId = json.getString("gameId"),
                state = TableState.fromWire(json.getString("state")) ?: error("Unsupported local game state"),
                profile = GameProfile.fromWire(json.getString("gameProfile")) ?: error("Unsupported local game format"),
                startingLife = json.getInt("startingLife"),
                autoEliminateAtZeroLife = json.optBoolean("autoEliminateAtZeroLife", true),
                askEliminationSource = json.optBoolean("askEliminationSource", true),
                active = json.optIntOrNull("active"),
                starter = json.optIntOrNull("starter"),
                winner = json.optIntOrNull("winner"),
                // Saved before shared victories: the single winner, if any.
                winners = json.optIntList("winners") ?: listOfNotNull(json.optIntOrNull("winner")),
                monarch = json.optIntOrNull("monarch"),
                initiative = json.optIntOrNull("initiative"),
                dayNight = com.turnhub.android.domain.DayNight.entries.firstOrNull { it.name == json.optString("dayNight") }
                    ?: com.turnhub.android.domain.DayNight.NEITHER,
                turnOrderReversed = json.optBoolean("turnOrderReversed"),
                winClaim = json.optIntOrNull("winClaim")?.takeIf { it in 0 until players.length() },
                winConfirmed = json.optIntList("winConfirmed")?.filter { it in 0 until players.length() }?.toSet().orEmpty(),
                autoEliminateAtPoisonLimit = json.optBoolean("autoEliminateAtPoisonLimit", true),
                autoEliminateAtCommanderDamage = json.optBoolean("autoEliminateAtCommanderDamage", true),
                poisonLimit = json.optInt("poisonLimit", 10),
                turnTimerMs = json.optLong("turnTimerMs", 0),
                timerRules = json.optJSONObject("timerRules")?.let { rules ->
                    com.turnhub.android.domain.LocalTimerRules(
                        com.turnhub.android.domain.TimeoutAction.valueOf(rules.getString("timeout")),
                        rules.getBoolean("earnExtensions"), rules.getInt("earnBeforePercent"), rules.getInt("maxEarnedPerGame"))
                } ?: com.turnhub.android.domain.LocalTimerRules(),
                turnExtensionMs = json.optLong("turnExtensionMs"),
                timeoutHandled = json.optBoolean("timeoutHandled"),
                twoHeadedGiant = json.optBoolean("twoHeadedGiant"),
                houseRules = json.optBoolean("houseRules"),
                startedAtMs = json.getLong("startedAtMs"),
                elapsedMs = json.getLong("elapsedMs"),
                runningSinceMs = json.optLongOrNull("runningSinceMs"),
                turnStartedAtElapsedMs = json.getLong("turnStartedAtElapsedMs"),
                revision = json.getLong("revision"),
                doomBag = json.optJSONObject("doomBag")?.let(::doomFromJson) ?: com.turnhub.android.protocol.DoomBag(),
                planechase = json.optJSONObject("planechase")?.let { Planechase.fromJson(it) } ?: Planechase(),
                pokemonCheckup = json.optJSONArray("pokemonCheckup")?.let { a -> (0 until a.length()).map { a.getString(it) } }.orEmpty(),
                radDue = json.optIntList("radDue")?.filter { it in 0 until players.length() }?.distinct().orEmpty(),
                players = (0 until players.length()).map { i ->
                    val p = players.getJSONObject(i)
                    val damage = p.getJSONObject("commanderDamage")
                    val legacySigil = p.optStringOrNull("localId")?.takeIf {
                        it.matches(Regex("sigil:[0-7]:[12]")) && p.optStringOrNull("profileId") == null }
                    LocalPlayer(
                        name = p.getString("name"),
                        localId = p.optStringOrNull("localId").takeIf { legacySigil == null },
                        sigilSeat = p.optStringOrNull("sigilSeat") ?: legacySigil,
                        profileId = p.optStringOrNull("profileId"),
                        guest = p.optBoolean("guest") || legacySigil != null,
                        life = p.getInt("life"),
                        eliminated = p.getBoolean("eliminated"),
                        conceded = p.optBoolean("conceded"),
                        eliminatedBy = p.optIntOrNull("eliminatedBy"),
                        selfEliminated = p.optBoolean("selfEliminated"), eliminationPending = p.optBoolean("eliminationPending"),
                        counters = p.optJSONObject("counters")?.let { c ->
                            com.turnhub.android.protocol.PlayerCounters(c.getInt("poison"), c.getInt("energy"), c.getInt("experience"),
                                c.getJSONArray("commanderCasts").let { a -> List(2) { a.getInt(it) } }, c.getBoolean("cityBlessing"),
                                treasure = c.optInt("treasure").coerceAtLeast(0), rad = c.optInt("rad").coerceAtLeast(0))
                        } ?: com.turnhub.android.protocol.PlayerCounters(),
                        outOrder = p.optIntOrNull("outOrder"),
                        turnsCompleted = p.getInt("turnsCompleted"),
                        demerits = p.optInt("demerits"),
                        extensionsEarned = p.optInt("extensionsEarned"),
                        extensionsUsed = p.optInt("extensionsUsed"),
                        turnMs = p.optLong("turnMs"),
                        fastestTurnMs = p.optLong("fastestTurnMs"),
                        longestTurnMs = p.optLong("longestTurnMs"),
                        commander = p.optStringOrNull("commander"),
                        commanderArt = p.optStringOrNull("commanderArt")?.takeIf { com.turnhub.android.data.TileCardGame.valid(it) },
                        cardOnly = p.optBoolean("cardOnly"),
                        // PokÃ©mon games saved before Lorcana kept this under "pokemon".
                        turn = (p.optJSONObject("turn") ?: p.optJSONObject("pokemon"))?.let { pk ->
                            fun <E : Enum<E>> names(key: String, values: List<E>): Set<E> = pk.optJSONArray(key)?.let { a ->
                                (0 until a.length()).mapNotNull { i -> values.firstOrNull { it.name == a.getString(i) } }.toSet()
                            }.orEmpty()
                            com.turnhub.android.domain.TurnStatus(
                                names("actions", com.turnhub.android.domain.TurnAction.entries),
                                names("conditions", com.turnhub.android.domain.SpecialCondition.entries))
                        } ?: com.turnhub.android.domain.TurnStatus(),
                        commanderDamage = damage.keys().asSequence().associate { key ->
                            val array = damage.getJSONArray(key)
                            key.toInt() to (0 until array.length()).map { array.getInt(it) }
                        },
                    )
                },
            ).let(DoomRules::resume).also { game ->
                require(game.players.size <= StandaloneGame.MAX_PLAYERS)
                require(DoomRules.valid(game))
                game.players.forEachIndexed { i, p ->
                    require(!p.selfEliminated || (p.eliminated && !p.conceded && p.eliminatedBy == null && !p.eliminationPending))
                    require(!p.eliminationPending || (game.askEliminationSource && game.state == TableState.PAUSED && p.eliminated && !p.conceded && p.eliminatedBy == null))
                    require(p.eliminatedBy == null || (p.eliminated && !p.conceded &&
                        p.eliminatedBy in game.players.indices && p.eliminatedBy != i &&
                        (!game.twoHeadedGiant || p.eliminatedBy / 2 != i / 2)))
                }
            }
        }
    }
}
