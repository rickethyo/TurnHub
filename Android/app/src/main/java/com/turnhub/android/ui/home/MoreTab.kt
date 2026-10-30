package com.turnhub.android.ui.home

import androidx.annotation.DrawableRes
import androidx.compose.foundation.Image
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.width
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
import androidx.compose.ui.platform.LocalUriHandler
import androidx.compose.ui.res.painterResource
import com.turnhub.android.R
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.dp
import com.turnhub.android.ui.components.AppVersionLabel
import com.turnhub.android.ui.components.BrassCard
import com.turnhub.android.ui.theme.DesignTokens
import com.turnhub.android.ui.theme.palette

internal enum class MorePage(val title: String) {
    HOME("More"), ABOUT("About TurnHub"), SETTINGS("Settings"), STORE("Store"), DONATE("Support TurnHub"), ATLAS("Atlas"),
}

/** Secondary destinations share the app shell, with one page visible at a time. */
@Composable
internal fun MoreTab(page: MorePage, onPage: (MorePage) -> Unit, atlas: @Composable ColumnScope.() -> Unit, settings: @Composable ColumnScope.() -> Unit) {
    val uriHandler = LocalUriHandler.current
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
                    MoreEntry("Atlas", "Connection and table administration", DesignTokens.Icons.hub) { onPage(MorePage.ATLAS) }
                    HorizontalDivider(color = palette.line)
                    MoreEntry("Settings", "Appearance, sound and connections", DesignTokens.Icons.sliders) { onPage(MorePage.SETTINGS) }
                    HorizontalDivider(color = palette.line)
                    MoreEntry("Support TurnHub", "Help support development", DesignTokens.Icons.info) { onPage(MorePage.DONATE) }
                    HorizontalDivider(color = palette.line)
                    MoreEntry("Store", "Not open yet", DesignTokens.Icons.hub) { onPage(MorePage.STORE) }
                }
                AppVersionLabel()
            }
            MorePage.ABOUT -> {
                BrassCard {
                    Text("More time around the table", color = palette.text, style = MaterialTheme.typography.titleMedium)
                    Text("TurnHub started with a simple idea: spend less time managing the game and more time enjoying it. " +
                        "I'm Ricky Buchanan, and I'm building TurnHub to bring turn timers, life totals, player profiles, " +
                        "and game statistics together in one place.", color = palette.muted)
                    Text("Designed for the tabletop and built to work locally, TurnHub keeps you connected to your game " +
                        "without requiring a cloud service, subscription, or internet connection.", color = palette.muted)
                    TextButton(onClick = { onPage(MorePage.DONATE) }) { Text("Support TurnHub") }
                }
                AppVersionLabel()
            }
            MorePage.DONATE -> BrassCard {
                Text("Help TurnHub grow", color = palette.text, style = MaterialTheme.typography.titleMedium)
                Text("I'm Ricky, creator of TurnHub. Your support enables me to build the tools that keep tabletop gaming " +
                    "focused on the game.", color = palette.muted)
                Text("If you'd like to support TurnHub's development, you can contribute through PayPal or Cash App. Thank you for your support.",
                    color = palette.muted)
                Text("Support is optional and does not unlock app features or purchase hardware or merchandise.",
                    color = palette.muted)
                TextButton(onClick = { uriHandler.openUri(TURNHUB_CASH_APP_URL) }) {
                    Text("Support with Cash App — \$TurnHub")
                }
                Image(painterResource(R.drawable.paypal_donate),
                    contentDescription = "Donate with PayPal",
                    modifier = Modifier.width(200.dp).height(56.dp).clickable(role = Role.Button) {
                        uriHandler.openUri(TURNHUB_DONATION_URL)
                    }.padding(vertical = 8.dp))
                Image(painterResource(R.drawable.turnhub_donation_qr),
                    contentDescription = "Scan with another device to donate to TurnHub through PayPal",
                    modifier = Modifier.size(256.dp).align(Alignment.CenterHorizontally))
            }
            MorePage.ATLAS -> atlas()
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

private const val TURNHUB_DONATION_URL = "https://www.paypal.com/donate/?business=JQQY4P6XE2ANL&no_recurring=0" +
    "&item_name=I%27m+Ricky%2C+creator+of+TurnHub.+Your+support+enables+me+to+build+the+tools+that+keep+tabletop+gaming+focused+on+the+game.&currency_code=USD"

private const val TURNHUB_CASH_APP_URL = "https://cash.app/\$TurnHub"
