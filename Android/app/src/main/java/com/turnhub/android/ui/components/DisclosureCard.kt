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
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.remember
import androidx.compose.runtime.withFrameNanos
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.relocation.BringIntoViewRequester
import androidx.compose.foundation.relocation.bringIntoViewRequester
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.stateDescription
import androidx.compose.ui.unit.dp
import androidx.compose.ui.geometry.Rect
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.layout.onSizeChanged
import androidx.compose.ui.unit.IntSize
import com.turnhub.android.ui.theme.DesignTokens
import com.turnhub.android.ui.theme.palette

/** Secondary controls stay one tap away without competing with the main task. */
@Composable
@OptIn(androidx.compose.foundation.ExperimentalFoundationApi::class)
fun DisclosureCard(title: String, initiallyExpanded: Boolean = false, content: @Composable ColumnScope.() -> Unit) {
    var expanded by rememberSaveable { mutableStateOf(initiallyExpanded) }
    var reveal by remember { mutableStateOf(false) }
    val requester = remember { BringIntoViewRequester() }
    val revealHeight = with(LocalDensity.current) { 144.dp.toPx() }
    var contentSize by remember { mutableStateOf(IntSize.Zero) }
    LaunchedEffect(expanded, contentSize) {
        if (expanded && reveal && contentSize.height > 0) {
            withFrameNanos { }
            // Include an actionable portion of the content, rather than a one-pixel anchor.
            requester.bringIntoView(Rect(0f, 0f, contentSize.width.toFloat(), minOf(contentSize.height.toFloat(), revealHeight)))
        }
    }
    BrassCard(contentPadding = PaddingValues(8.dp)) {
        FilledTonalButton(onClick = { reveal = true; expanded = !expanded },
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
        if (expanded) {
            androidx.compose.foundation.layout.Column(Modifier.fillMaxWidth().onSizeChanged { contentSize = it }.bringIntoViewRequester(requester),
                verticalArrangement = androidx.compose.foundation.layout.Arrangement.spacedBy(8.dp), content = content)
        }
    }
}
