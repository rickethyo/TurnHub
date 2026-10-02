package com.turnhub.android.ui.components

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.RowScope
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.unit.dp
import com.turnhub.android.ui.theme.DesignTokens
import com.turnhub.android.ui.theme.palette

/** Collects the rows of a [GroupedList]. */
class GroupedListScope internal constructor() {
    internal val rows = mutableListOf<Pair<Modifier, @Composable RowScope.() -> Unit>>()

    /** One row: at least a touch target tall, content laid out left to right. */
    fun row(modifier: Modifier = Modifier, content: @Composable RowScope.() -> Unit) {
        rows += modifier to content
    }
}

/**
 * An inset grouped list, as in the phone's own Settings: rows on one rounded
 * surface separated by hairlines that start after the leading inset. An
 * optional [header] sits above the group and a [footer] explains it below.
 */
@Composable
fun GroupedList(
    modifier: Modifier = Modifier,
    header: String? = null,
    footer: String? = null,
    content: GroupedListScope.() -> Unit,
) {
    val p = palette
    val rows = GroupedListScope().apply(content).rows
    val shape = RoundedCornerShape(if (p.ornament) 14.dp else DesignTokens.Radius.md)
    Column(modifier.fillMaxWidth(), verticalArrangement = Arrangement.spacedBy(6.dp)) {
        header?.let {
            Text(
                it.uppercase(),
                color = p.muted,
                style = MaterialTheme.typography.labelMedium,
                modifier = Modifier.padding(horizontal = 16.dp),
            )
        }
        Column(
            Modifier
                .fillMaxWidth()
                .clip(shape)
                .background(if (p.dark) p.surface2 else p.surface)
                .then(if (p.ornament || p.bg == androidx.compose.ui.graphics.Color.Black) Modifier.border(1.dp, p.line, shape) else Modifier),
        ) {
            rows.forEachIndexed { index, (rowModifier, rowContent) ->
                if (index > 0) {
                    Box(
                        Modifier
                            .padding(start = 16.dp)
                            .fillMaxWidth()
                            .height(1.dp)
                            .background(p.line),
                    )
                }
                Row(
                    Modifier
                        .fillMaxWidth()
                        .heightIn(min = 52.dp)
                        .then(rowModifier)
                        .padding(horizontal = 16.dp, vertical = 10.dp),
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(12.dp),
                    content = rowContent,
                )
            }
        }
        footer?.let {
            Text(
                it,
                color = p.faint,
                style = MaterialTheme.typography.bodySmall,
                modifier = Modifier.padding(horizontal = 16.dp),
            )
        }
    }
}
