package com.turnhub.android.ui.home

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.selection.toggleable
import androidx.compose.foundation.clickable
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.ModalBottomSheet
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.rememberModalBottomSheetState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.unit.dp
import com.turnhub.android.R
import com.turnhub.android.data.AccountInfo
import com.turnhub.android.data.AdminState
import com.turnhub.android.protocol.AccountPermission
import com.turnhub.android.protocol.AvatarIcon
import com.turnhub.android.protocol.SessionInfo
import com.turnhub.android.ui.components.BrassCard
import com.turnhub.android.ui.components.Eyebrow
import com.turnhub.android.ui.components.GroupedList
import com.turnhub.android.ui.components.PlayerAvatar
import com.turnhub.android.ui.components.StatusBadge
import com.turnhub.android.ui.components.Tone
import com.turnhub.android.ui.theme.DesignTokens
import com.turnhub.android.ui.theme.palette

/*
 * People: one list of accounts for Admins and Game Masters, as in the portal.
 * Selecting a person opens a sheet with only what applies to them right now:
 * moderation while they are at the table (Game Master), nudges, roles that
 * save as they change (Admin) and archiving. Atlas still validates everything.
 */

private val ROLES = listOf(
    Triple(AccountPermission.ADMIN, "Admin", "Device settings, Wi-Fi, updates and people."),
    Triple(AccountPermission.GAME_MASTER, "Game Master", "Moderates players at the table."),
    Triple(AccountPermission.DEVELOPER, "Developer", "The diagnostics page and the Atlas log."),
    Triple(AccountPermission.TABLET_ACCESS, "Tablet access",
        "Turns on tablet mode without a code from the Atlas screen, e.g. for a tablet account that never joins the table."),
)
private val GM_POWERS = listOf(
    Triple(AccountPermission.GM_RESET_CONNECTIONS, "Reset connections", "Can sign a player out everywhere until they sign in again."),
    Triple(AccountPermission.GM_REMOVE_FROM_GAME, "Remove from game", "Can take a player out of the game."),
)
private const val MODERATION_BITS = 8 or 16

private fun AccountInfo.displayName() = name.ifBlank { "Unnamed account" }

private fun AccountInfo.roleSummary(): String =
    ROLES.filter { has(it.first) }.joinToString(" · ") { it.second }.ifEmpty { "Player" }

/** The People card on the Players tab, for Admins and Game Masters. */
@Composable
fun PeopleCard(info: SessionInfo, admin: AdminState, avatars: List<AvatarIcon>, actions: AdminActions) {
    val p = palette
    var openId by rememberSaveable { mutableStateOf<String?>(null) }
    var showArchived by rememberSaveable { mutableStateOf(false) }
    val live = admin.accounts.filter { !it.archived }
    val archived = admin.accounts.filter { it.archived }
    BrassCard {
        Eyebrow("People") {
            TextButton(onClick = actions.onRefresh) { Text("Refresh", color = p.muted) }
        }
        val hint = when {
            !info.has(AccountPermission.ADMIN) -> "moderate them."
            info.has(AccountPermission.GAME_MASTER) -> "change their roles or moderate them."
            else -> "change their roles."
        }
        Text(
            "Everyone with an account on this Atlas. Select a person to $hint",
            color = p.muted,
            style = MaterialTheme.typography.bodySmall,
        )
        if (live.isEmpty()) EmptyNote("No accounts yet.")
        else PeopleList(live, info, avatars) { openId = it }
        if (archived.isNotEmpty()) {
            TextButton(onClick = { showArchived = !showArchived }) {
                Text("${if (showArchived) "Hide" else "Show"} archived accounts (${archived.size})", color = p.muted)
            }
            if (showArchived) PeopleList(archived, info, avatars) { openId = it }
        }
    }
    admin.accounts.firstOrNull { it.profileId == openId }?.let { account ->
        PersonSheet(account, info, admin, avatars, actions, onDismiss = { openId = null })
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun PeopleList(people: List<AccountInfo>, info: SessionInfo, avatars: List<AvatarIcon>, onOpen: (String) -> Unit) {
    val p = palette
    GroupedList {
        people.forEach { account ->
            val me = account.profileId == info.profileId
            row(Modifier.clickable(role = Role.Button, onClickLabel = "Manage ${account.displayName()}") { onOpen(account.profileId) }) {
                PlayerAvatar(account.displayName(), 0, avatars.firstOrNull { it.id == account.avatar }, size = 40.dp)
                Column(Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(2.dp)) {
                    Text(account.displayName() + if (me) " (you)" else "", color = p.text, style = MaterialTheme.typography.titleSmall)
                    Text(account.roleSummary(), color = p.muted, style = MaterialTheme.typography.bodySmall)
                    if (account.atTable || account.reconnectRequired || account.nudgeMuted) {
                        FlowRow(horizontalArrangement = Arrangement.spacedBy(6.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
                            if (account.atTable) StatusBadge("At the table", Tone.ACTIVE)
                            if (account.reconnectRequired) StatusBadge("Must sign in again", Tone.WARN)
                            if (account.nudgeMuted) StatusBadge("Nudges muted", Tone.WARN)
                        }
                    }
                }
                Icon(painterResource(R.drawable.ic_th_chevron_right), contentDescription = null, tint = p.faint, modifier = Modifier.size(18.dp))
            }
        }
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun PersonSheet(
    account: AccountInfo,
    info: SessionInfo,
    admin: AdminState,
    avatars: List<AvatarIcon>,
    actions: AdminActions,
    onDismiss: () -> Unit,
) {
    val p = palette
    val me = account.profileId == info.profileId
    val isAdmin = info.has(AccountPermission.ADMIN)
    val isGm = info.has(AccountPermission.GAME_MASTER)
    val live = !account.archived
    val name = account.displayName()
    var confirm by remember { mutableStateOf<Confirmation?>(null) }

    fun applyModeration(action: String, done: String) = actions.run {
        moderate(account.profileId, action, done)
        refresh(admin = isAdmin, gameMaster = true)
    }

    fun saveRoles(bits: Int) {
        val clean = if (bits and AccountPermission.GAME_MASTER.bit == 0) bits and MODERATION_BITS.inv() else bits
        val save = {
            actions.run {
                savePermissions(account.profileId, clean)
                refresh(admin = true, gameMaster = isGm)
            }
            if (me) actions.onRefresh()
        }
        if (me && account.has(AccountPermission.ADMIN) && clean and AccountPermission.ADMIN.bit == 0) {
            confirm = Confirmation(
                "Remove your own Admin role?",
                "You lose Device Settings and people management straight away. Another Admin would have to give it back.",
                "Remove Admin",
                save,
            )
        } else save()
    }

    confirm?.let { c ->
        ConfirmDialog(c.title, c.text, c.ok, onConfirm = { confirm = null; c.action() }, onDismiss = { confirm = null })
    }

    ModalBottomSheet(
        onDismissRequest = onDismiss,
        sheetState = rememberModalBottomSheetState(skipPartiallyExpanded = true),
        containerColor = p.bg,
    ) {
        Column(
            Modifier
                .fillMaxWidth()
                .verticalScroll(rememberScrollState())
                .padding(horizontal = DesignTokens.Layout.gutter)
                .navigationBarsPadding()
                .padding(bottom = DesignTokens.Space.s5),
            verticalArrangement = Arrangement.spacedBy(DesignTokens.Space.s5),
        ) {
            Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(14.dp)) {
                PlayerAvatar(name, 0, avatars.firstOrNull { it.id == account.avatar }, size = 52.dp)
                Column(Modifier.weight(1f)) {
                    Text(
                        name + if (me) " (you)" else "",
                        color = p.text,
                        style = MaterialTheme.typography.headlineSmall,
                        modifier = Modifier.semantics { heading() },
                    )
                    Text(
                        account.profileId + (if (account.archived) " · Archived" else "") +
                            if (!account.hasPin) " · No PIN or password" else "",
                        color = p.faint,
                        style = MaterialTheme.typography.bodySmall.copy(fontFamily = FontFamily.Monospace),
                    )
                }
            }
            AdminMessage(admin, actions)

            if (isGm && live && account.atTable) {
                val noPin = if (!account.hasPin) "Needs a PIN or password on their account first." else null
                GroupedList(header = "At the table", footer = "Pass works only while it is their turn.") {
                    actionRow(
                        R.drawable.ic_th_pass, "Pass their turn", "Ends their turn now, without the pass grace period.",
                        blockedBy = null, danger = false,
                    ) {
                        confirm = Confirmation("Pass $name's turn?", "Their turn ends now and the next player starts.", "Pass turn") {
                            applyModeration("pass", "Passed $name's turn.")
                        }
                    }
                    if (info.has(AccountPermission.GM_RESET_CONNECTIONS)) {
                        actionRow(
                            R.drawable.ic_th_refresh, "Reset connections",
                            "Signs them out everywhere and pauses their Sigil until they sign in again. Their seat and life stay.",
                            blockedBy = if (account.reconnectRequired) "Waiting for them to sign in again." else noPin, danger = false,
                        ) {
                            confirm = Confirmation(
                                "Reset $name's connections?",
                                "Every phone they use is signed out and their Sigil pauses until they sign in again with their PIN. " +
                                    "Their seat and life total stay.",
                                "Reset connections",
                            ) { applyModeration("reset", "$name must sign in again.") }
                        }
                    }
                    if (info.has(AccountPermission.GM_REMOVE_FROM_GAME)) {
                        actionRow(
                            R.drawable.ic_th_close, "Remove from game", "Takes them out of this game. In a match it counts as a concession.",
                            blockedBy = noPin, danger = true,
                        ) {
                            confirm = Confirmation(
                                "Remove $name from the game?",
                                "They leave the table and are signed out everywhere. In a match this counts as a concession.",
                                "Remove",
                            ) { applyModeration("remove", "$name was removed from the game.") }
                        }
                    }
                }
            }

            if (isGm && live) {
                GroupedList(header = "Nudges") {
                    switchRow(
                        "Can send nudges", "Turn off to stop this account from nudging other players.",
                        checked = !account.nudgeMuted, enabled = !admin.busy, nested = false,
                    ) { on ->
                        if (on) applyModeration("unmute", "$name can send nudges again.")
                        else applyModeration("mute", "$name can no longer send nudges.")
                    }
                }
            }

            if (isAdmin && live) {
                val locked = !account.hasPin && account.permissions == 0
                GroupedList(
                    header = "Roles",
                    footer = if (locked) "Roles need a PIN or password on this account. They can set one in My Account."
                    else "Changes save as you make them.",
                ) {
                    ROLES.forEach { (permission, label, text) ->
                        val fixed = permission == AccountPermission.ADMIN && account.primary
                        switchRow(
                            label, if (fixed) "The initial Admin always keeps this role." else text,
                            checked = account.has(permission), enabled = !fixed && !locked && !admin.busy, nested = false,
                        ) { on -> saveRoles(if (on) account.permissions or permission.bit else account.permissions and permission.bit.inv()) }
                        if (permission == AccountPermission.GAME_MASTER) {
                            GM_POWERS.forEach { (power, powerLabel, powerText) ->
                                switchRow(
                                    powerLabel, powerText,
                                    checked = account.has(power),
                                    enabled = !locked && !admin.busy && account.has(AccountPermission.GAME_MASTER),
                                    nested = true,
                                ) { on -> saveRoles(if (on) account.permissions or power.bit else account.permissions and power.bit.inv()) }
                            }
                        }
                    }
                }
            }

            if (isAdmin) {
                val blocked = when {
                    account.primary -> "The initial Admin cannot be archived."
                    account.atTable && live -> "They must leave the table first."
                    else -> null
                }
                GroupedList {
                    if (live) {
                        actionRow(
                            null, "Archive account", "Blocks sign-in and Sigil use. Statistics stay saved, and you can restore it later.",
                            blockedBy = blocked, danger = true,
                        ) {
                            confirm = Confirmation(
                                "Archive $name?",
                                "They can no longer sign in or use a Sigil. Their statistics stay saved, and you can restore the account later.",
                                "Archive",
                            ) {
                                actions.run { archive(account.profileId, true); refresh(admin = true, gameMaster = isGm) }
                            }
                        }
                    } else {
                        actionRow(null, "Restore account", "Lets them sign in again, with the roles they had.", blockedBy = null, danger = false) {
                            actions.run { archive(account.profileId, false); refresh(admin = true, gameMaster = isGm) }
                        }
                    }
                }
            }
        }
    }
}

private class Confirmation(val title: String, val text: String, val ok: String, val action: () -> Unit)

/** A tappable action with a one-line explanation; [blockedBy] disables it and says why. */
private fun com.turnhub.android.ui.components.GroupedListScope.actionRow(
    icon: Int?,
    title: String,
    detail: String,
    blockedBy: String?,
    danger: Boolean,
    onClick: () -> Unit,
) {
    row(
        Modifier
            .clickable(enabled = blockedBy == null, role = Role.Button, onClick = onClick)
            .alpha(if (blockedBy == null) 1f else .55f),
    ) {
        val p = palette
        val color = if (danger) p.bad else p.text
        if (icon != null) Icon(painterResource(icon), contentDescription = null, tint = if (danger) p.bad else p.accent, modifier = Modifier.size(22.dp))
        Column(Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(2.dp)) {
            Text(title, color = color, style = MaterialTheme.typography.bodyLarge)
            Text(blockedBy ?: detail, color = p.muted, style = MaterialTheme.typography.bodySmall)
        }
    }
}

/** A switch row; [nested] indents a Game Master power under its role. */
private fun com.turnhub.android.ui.components.GroupedListScope.switchRow(
    label: String,
    detail: String,
    checked: Boolean,
    enabled: Boolean,
    nested: Boolean,
    onChange: (Boolean) -> Unit,
) {
    row(Modifier.toggleable(value = checked, enabled = enabled, role = Role.Switch, onValueChange = onChange)) {
        val p = palette
        if (nested) Spacer(Modifier.width(14.dp))
        Column(Modifier.weight(1f).alpha(if (enabled) 1f else .6f), verticalArrangement = Arrangement.spacedBy(2.dp)) {
            Text(label, color = p.text, style = MaterialTheme.typography.bodyLarge)
            Text(detail, color = p.muted, style = MaterialTheme.typography.bodySmall)
        }
        Switch(checked = checked, onCheckedChange = null, enabled = enabled)
    }
}
