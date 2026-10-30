package com.turnhub.android.ui.home

import androidx.annotation.DrawableRes
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.dp
import com.turnhub.android.ui.components.AppVersionLabel
import com.turnhub.android.ui.components.BrassCard
import com.turnhub.android.ui.theme.DesignTokens
import com.turnhub.android.ui.theme.palette

internal enum class MorePage(val title: String) {
    HOME("More"), ABOUT("About TurnHub"), SETTINGS("Settings"), STORE("Store"),
}

/** Secondary destinations share the app shell, with one page visible at a time. */
@Composable
internal fun MoreTab(page: MorePage, onPage: (MorePage) -> Unit, settings: @Composable ColumnScope.() -> Unit) {
    Column(verticalArrangement = Arrangement.spacedBy(14.dp)) {
        if (page != MorePage.HOME) {
            TextButton(onClick = { onPage(MorePage.HOME) }) {
                Icon(painterResource(DesignTokens.Icons.chevronLeft), contentDescription = null,
                    modifier = Modifier.size(20.dp))
                Text("Back to More")
            }
        }
        Text(page.title, color = palette.text, style = MaterialTheme.typography.headlineSmall,
            modifier = Modifier.semantics { heading() })
        when (page) {
            MorePage.HOME -> {
                BrassCard {
                    MoreEntry("About TurnHub", "The idea behind the project", DesignTokens.Icons.info) { onPage(MorePage.ABOUT) }
                    HorizontalDivider(color = palette.line)
                    MoreEntry("Settings", "Appearance, sound and connections", DesignTokens.Icons.sliders) { onPage(MorePage.SETTINGS) }
                    HorizontalDivider(color = palette.line)
                    MoreEntry("Store", "Not open yet", DesignTokens.Icons.hub) { onPage(MorePage.STORE) }
                }
                AppVersionLabel()
            }
            MorePage.ABOUT -> {
                BrassCard {
                    Text("More time around the table", color = palette.text, style = MaterialTheme.typography.titleMedium)
                    Text("TurnHub brings turns, timers, life totals and game records together on your phone or tablet, " +
                        "with Atlas and Sigils for a connected table.", color = palette.muted)
                    Text("Less bookkeeping between turns. More room for the people and games at your table.", color = palette.muted)
                }
                AppVersionLabel()
            }
            MorePage.SETTINGS -> settings()
            MorePage.STORE -> BrassCard {
                Text("The store is not open yet.", color = palette.text, style = MaterialTheme.typography.titleMedium)
            }
        }
    }
}

@Composable
private fun MoreEntry(title: String, description: String, @DrawableRes icon: Int, onClick: () -> Unit) {
    Row(Modifier.fillMaxWidth().clickable(role = Role.Button, onClick = onClick)
        .heightIn(min = 72.dp).padding(vertical = 12.dp),
        verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(12.dp)) {
        Icon(painterResource(icon), contentDescription = null, tint = palette.accent, modifier = Modifier.size(24.dp))
        Column(Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(4.dp)) {
            Text(title, color = palette.text, style = MaterialTheme.typography.titleMedium)
            Text(description, color = palette.muted, style = MaterialTheme.typography.bodySmall)
        }
        Icon(painterResource(DesignTokens.Icons.chevronRight), contentDescription = null,
            tint = palette.muted, modifier = Modifier.size(20.dp))
    }
}
