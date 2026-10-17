package com.turnhub.android.ui.tablet

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalUriHandler
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.turnhub.android.ui.components.ToneButton

/** A reference only: opening or closing it never passes a turn or applies effects. */
@Composable
internal fun DeathMayDieTurnPhasesButton() {
    var open by rememberSaveable { mutableStateOf(false) }
    val uri = LocalUriHandler.current
    ToneButton("Phases of your turn", { open = true })
    if (open) AlertDialog(onDismissRequest = { open = false }, title = { Text("Phases of your turn") }, text = {
        Column(Modifier.heightIn(max = 440.dp).verticalScroll(rememberScrollState()), verticalArrangement = Arrangement.spacedBy(10.dp)) {
            Text("1 · Take actions", fontWeight = FontWeight.SemiBold)
            Text("Take 3 actions, plus any granted by your abilities or other effects.")
            Text("2 · Draw a Mythos card", fontWeight = FontWeight.SemiBold)
            Text("Resolve the card, including enemy summons. Leave its summoning symbol visible in the discard pile.")
            Text("3 · Investigate or fight", fontWeight = FontWeight.SemiBold)
            Text("Safe: draw and resolve a Discovery card. Enemies present: resolve their attacks instead. Enemies entering your space during this phase also attack.")
            Text("4 · End your turn", fontWeight = FontWeight.SemiBold)
            Text("In order: resolve non–Elder One end effects; resolve Fire; check Mythos discards for advancement and reshuffle; check for summoning; resolve revealed Elder One end effects in stage order.")
            Text("Then use Pass. Apply episode and expansion exceptions from your physical game. If you died this turn, only the Mythos-discard check remains.", style = MaterialTheme.typography.bodySmall)
            TextButton(onClick = { uri.openUri("https://resources.cmon.com/DMD_Rulebook_web.pdf") }) { Text("CMON rulebook") }
        }
    }, confirmButton = { TextButton({ open = false }) { Text("Close") } })
}
