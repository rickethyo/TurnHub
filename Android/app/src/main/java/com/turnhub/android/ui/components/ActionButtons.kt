package com.turnhub.android.ui.components

import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.layout.RowScope
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.OutlinedButton
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.compositeOver
import androidx.compose.ui.graphics.lerp
import androidx.compose.ui.graphics.luminance
import androidx.compose.ui.unit.dp
import com.turnhub.android.ui.theme.palette

/** Dialog/footer roles are explicit, shared by local and remote screens in every theme. */
enum class ActionRole { PRIMARY, SECONDARY, DESTRUCTIVE }

@Composable
fun ActionButton(onClick: () -> Unit, modifier: Modifier = Modifier, enabled: Boolean = true,
    role: ActionRole = ActionRole.SECONDARY, content: @Composable RowScope.() -> Unit) {
    val p = palette
    val background = when (role) {
        ActionRole.PRIMARY -> p.accent
        ActionRole.SECONDARY -> p.surface2
        ActionRole.DESTRUCTIVE -> p.bad.copy(alpha = .14f).compositeOver(p.surface)
    }
    val color = when (role) {
        ActionRole.PRIMARY -> p.accent
        ActionRole.SECONDARY -> p.text
        ActionRole.DESTRUCTIVE -> readableActionColor(p.bad, background)
    }
    OutlinedButton(onClick, modifier.heightIn(min = 48.dp), enabled,
        shape = RoundedCornerShape(12.dp), border = BorderStroke(1.dp, if (role == ActionRole.SECONDARY) p.lineStrong else color),
        colors = ButtonDefaults.outlinedButtonColors(
            containerColor = background, contentColor = if (role == ActionRole.PRIMARY) p.onAccent else color),
        contentPadding = PaddingValues(horizontal = 14.dp, vertical = 10.dp), content = content)
}

/** Keep the danger hue while making small labels readable on either theme's surface. */
private fun readableActionColor(foreground: Color, background: Color): Color {
    val base = background.luminance()
    val end = if (base >= .179f) Color.Black else Color.White
    for (step in 0..20) {
        val candidate = lerp(foreground, end, step / 20f)
        val light = candidate.luminance()
        if ((maxOf(light, base) + .05f) / (minOf(light, base) + .05f) >= 4.5f) return candidate
    }
    return end
}
