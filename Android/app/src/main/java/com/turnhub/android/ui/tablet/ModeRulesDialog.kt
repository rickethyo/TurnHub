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
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalUriHandler
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.dp
import com.turnhub.android.protocol.GameProfile
import com.turnhub.android.standalone.modeRules
import com.turnhub.android.ui.theme.palette

/** "? Rules": the mode's rules in short, with a link to the publisher's full rules. */
@Composable
internal fun ModeRulesButton(profile: GameProfile, teams: Boolean, modifier: Modifier = Modifier) {
    var open by remember { mutableStateOf(false) }
    val rules = modeRules(profile, teams)
    TextButton({ open = true }, modifier.semantics { contentDescription = "Rules for ${rules.title}" }) {
        Text("? Rules", color = palette.text)
    }
    if (open) ModeRulesDialog(profile, teams) { open = false }
}

@Composable
internal fun ModeRulesDialog(profile: GameProfile, teams: Boolean, onDismiss: () -> Unit) {
    val p = palette
    val rules = modeRules(profile, teams)
    val links = LocalUriHandler.current
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(rules.title) },
        text = {
            Column(Modifier.heightIn(max = 480.dp).verticalScroll(rememberScrollState()), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                (rules.official?.summary ?: rules.customSummary).forEach { Text("• $it", color = p.text) }
                rules.official?.let { official ->
                    Text("Summarized from the ${official.sourceName}. Numbers in brackets are rule numbers there.",
                        color = p.muted, style = MaterialTheme.typography.bodySmall)
                    TextButton({ links.openUri(official.sourceUrl) }) { Text("Read the official rules") }
                }
            }
        },
        confirmButton = { TextButton(onDismiss) { Text("Close") } },
    )
}
