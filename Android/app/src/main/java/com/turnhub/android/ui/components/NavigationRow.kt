package com.turnhub.android.ui.components

import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.*
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.unit.dp
import com.turnhub.android.ui.theme.DesignTokens
import com.turnhub.android.ui.theme.palette

/** Opening an option has the same target and value/chevron layout on every device. */
@Composable
fun NavigationRow(title: String, value: String? = null, modifier: Modifier = Modifier,
    enabled: Boolean = true, leadingIcon: Int? = null, onClick: () -> Unit) {
    Surface(onClick = onClick, enabled = enabled, modifier = modifier.fillMaxWidth().heightIn(min = 56.dp),
        shape = RoundedCornerShape(12.dp), color = palette.surface2, border = BorderStroke(1.dp, palette.lineStrong)) {
        Row(Modifier.padding(14.dp), verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(12.dp)) {
            leadingIcon?.let { Icon(painterResource(it), contentDescription = null, tint = palette.accent, modifier = Modifier.size(24.dp)) }
            Column(Modifier.weight(1f)) {
                Text(title, color = palette.text, style = MaterialTheme.typography.titleSmall)
                value?.let { Text(it, color = palette.muted, style = MaterialTheme.typography.bodyMedium) }
            }
            Icon(painterResource(DesignTokens.Icons.chevronRight), contentDescription = null, tint = palette.muted)
        }
    }
}
