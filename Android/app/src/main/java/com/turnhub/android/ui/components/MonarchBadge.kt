package com.turnhub.android.ui.components

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.size
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.unit.dp
import com.turnhub.android.R
import com.turnhub.android.ui.theme.palette

/** Crown and words together: the icon is decorative, the label names the holder. */
@Composable
fun MonarchBadge(name: String? = null, modifier: Modifier = Modifier) {
    Row(modifier, verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(6.dp)) {
        Icon(painterResource(R.drawable.ic_th_crown), contentDescription = null, tint = palette.accent, modifier = Modifier.size(22.dp))
        Text(name?.let { "Monarch: $it" } ?: "Monarch", color = palette.accent, style = MaterialTheme.typography.labelLarge)
    }
}
