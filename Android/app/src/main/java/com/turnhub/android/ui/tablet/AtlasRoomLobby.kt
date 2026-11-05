package com.turnhub.android.ui.tablet

import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
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
    Column(Modifier.fillMaxWidth().verticalScroll(rememberScrollState()).padding(16.dp),
        verticalArrangement=Arrangement.spacedBy(16.dp)) {
        BrassCard {
            Text("Your Atlas table",style=MaterialTheme.typography.headlineSmall)
            Text("Choose your profile to join the hub phone's game. Your local game stays saved.")
            state.preview?.players?.forEachIndexed { index,player -> Text("${index+1}. ${player.name}") }
        }
        BrassCard {
            Text("Choose your profile",style=MaterialTheme.typography.titleMedium)
            FlowRow(horizontalArrangement=Arrangement.spacedBy(8.dp),verticalArrangement=Arrangement.spacedBy(8.dp)) {
                state.directory.forEach { profile -> ToneButton(profile.name,{model.joinAtlasProfile(profile.id)},enabled=!state.busy) }
            }
            if(state.directory.isEmpty()) Text("Waiting for the hub's profiles…")
            state.message?.let { Text(it) }
            ToneButton("View the table",{model.join("display",0)},enabled=!state.busy)
        }
    }
}
