package com.turnhub.android.ui.tablet

import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.SuggestionChip
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import com.turnhub.android.lan.LanUiState
import com.turnhub.android.lan.LanViewModel
import com.turnhub.android.ui.components.BrassCard
import com.turnhub.android.ui.components.ToneButton

@OptIn(ExperimentalLayoutApi::class)
@Composable
internal fun AtlasRoomLobby(state:LanUiState,model:LanViewModel) {
    val game = state.preview ?: return
    TablePreview(game.toSummary(System.currentTimeMillis(), com.turnhub.android.domain.TableClock.nowMs()), reduceMotion = true) {
    Column(Modifier.fillMaxWidth().heightIn(max = 480.dp).verticalScroll(rememberScrollState()),
        verticalArrangement=Arrangement.spacedBy(16.dp)) {
        BrassCard {
            Text("Your Atlas table",style=MaterialTheme.typography.headlineSmall, color=com.turnhub.android.ui.theme.palette.text)
            Text("Pick your player to control their seat. The game stays visible behind this picker.", color=com.turnhub.android.ui.theme.palette.muted)
            Text("${game.players.size} players at the table", color=com.turnhub.android.ui.theme.palette.muted)
        }
        BrassCard {
            Text("Choose your profile",style=MaterialTheme.typography.titleMedium, color=com.turnhub.android.ui.theme.palette.text)
            FlowRow(horizontalArrangement=Arrangement.spacedBy(8.dp),verticalArrangement=Arrangement.spacedBy(8.dp)) {
                state.directory.forEach { profile ->
                    SuggestionChip(onClick={model.joinAtlasProfile(profile.id)}, enabled=!state.busy,
                        label={ Column {
                            Text(profile.name)
                            if(state.directory.count { it.name == profile.name } > 1) Text(profile.id)
                        } }, icon={
                            com.turnhub.android.ui.components.PlayerAvatar(profile.name,profile.id.hashCode(),
                                com.turnhub.android.standalone.LocalPersonalization(avatar=profile.avatar).icon(),size=32.dp,
                                color=com.turnhub.android.ui.components.playerColor(profile.color,
                                    com.turnhub.android.ui.theme.palette.avatarColor(profile.id.hashCode())))
                        })
                }
            }
            if(state.directory.isEmpty()) Text("Waiting for the hub's profiles…")
            state.message?.let { Text(it, color=com.turnhub.android.ui.theme.palette.warn) }
            ToneButton("View the table",{model.join("display",0)},enabled=!state.busy)
        }
    }
}
}
