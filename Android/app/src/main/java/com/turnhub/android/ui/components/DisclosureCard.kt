package com.turnhub.android.ui.components

import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.FilledTonalButton
import androidx.compose.material3.ButtonDefaults
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.stateDescription
import androidx.compose.ui.unit.dp
import com.turnhub.android.ui.theme.DesignTokens
import com.turnhub.android.ui.theme.palette

/** Secondary controls stay one tap away without competing with the main task. */
@Composable
fun DisclosureCard(title: String, initiallyExpanded: Boolean = false, content: @Composable ColumnScope.() -> Unit) {
    var expanded by rememberSaveable { mutableStateOf(initiallyExpanded) }
    BrassCard(contentPadding = PaddingValues(8.dp)) {
        FilledTonalButton(onClick = { expanded = !expanded },
            shape = RoundedCornerShape(if (palette.ornament) 12.dp else DesignTokens.Radius.md),
            colors = ButtonDefaults.filledTonalButtonColors(containerColor = palette.fillStrong, contentColor = palette.text),
            modifier = Modifier.fillMaxWidth().heightIn(min = 48.dp).semantics {
            stateDescription = if (expanded) "Expanded" else "Collapsed"
        }) {
            Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
                Text(title, color = palette.text, style = MaterialTheme.typography.titleSmall, modifier = Modifier.weight(1f))
                Icon(painterResource(if (expanded) DesignTokens.Icons.chevronUp else DesignTokens.Icons.chevronDown),
                    contentDescription = null, tint = palette.muted)
            }
        }
        if (expanded) content()
    }
}
