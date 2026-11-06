package com.turnhub.android.ui.tablet

import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import com.turnhub.android.lan.*
import com.turnhub.android.standalone.*
import kotlinx.coroutines.launch
import com.turnhub.android.ui.components.*
import com.turnhub.android.ui.theme.palette

@Composable
internal fun SharedPlanarSetup(state: LanUiState, model: LanViewModel) {
    val game = state.remote ?: return
    val planar = game.planechase
    val enabled = state.online && !state.busy && !state.uncertain && (state.role in setOf("player", "table") || state.master)
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    var browser by remember { mutableStateOf(false) }
    val cards = PlaneLibrary.known(context)?.values?.toList().orEmpty()
    if (browser) PlanarDeckBrowser(cards, planar.selection, {
        model.setup(game, "planar-selection", "selection" to it.toJson()); true
    }) { browser = false }
    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp)) {
        BrassCard {
            Text("Planechase", style = MaterialTheme.typography.titleLarge, color = palette.text)
            SetupSwitch("Play Planechase", null, planar.enabled, enabled) { model.setup(game, "planechase", "on" to it) }
            if (planar.enabled) {
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    PlanarDeckSource.entries.forEach { source -> FilterChip(planar.source == source,
                        { model.setup(game, "planar-source", "source" to source.wireValue) },
                        label = { Text(if (source == PlanarDeckSource.DIGITAL) "Digital deck" else "Physical cards") }, enabled = enabled) }
                }
                if (planar.source == PlanarDeckSource.DIGITAL) {
                    SetupSwitch("Include phenomena", "Up to two for each player, as the official rules allow.", planar.phenomena, enabled) {
                        model.setup(game, "planar-phenomena", "on" to it)
                    }
                    Text("Selected deck: ${planar.selection.name}", color = palette.muted)
                    if (cards.isNotEmpty()) ToneButton("Choose plane cards", { browser = true }, enabled = enabled)
                    else ToneButton("Download the planes", { scope.launch { PlaneLibrary.download(context) } }, enabled = !PlaneLibrary.downloading)
                }
            }
        }
        DisclosureCard("Planechase rules") {
            Text("Roll the planar die during your main phase with an empty stack. The first roll is free; each extra roll costs one more mana. Resolve each plane's instructions at the table.", color = palette.text)
        }
    }
}
