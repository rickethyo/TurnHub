package com.turnhub.android.ui.components

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.selection.selectable
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.luminance
import androidx.compose.ui.graphics.toArgb
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.dp
import com.turnhub.android.ui.theme.palette
import java.util.Locale

private val playerColors = listOf(
    "Crimson" to "#EF5350", "Ember" to "#FF9800", "Gold" to "#FDD835",
    "Leaf" to "#7CB342", "Jade" to "#26A69A", "Sky" to "#29B6F6",
    "Sapphire" to "#5C6BC0", "Violet" to "#AB47BC", "Rose" to "#EC407A",
    "Pearl" to "#ECEFF1",
)

internal fun playerColor(hex: String?, fallback: Color): Color =
    hex?.let { runCatching { Color(android.graphics.Color.parseColor(it)) }.getOrNull() } ?: fallback

internal fun colorInk(color: Color): Color = if (color.luminance() > .4f) Color.Black else Color.White

/** Named, accessible swatches with a full HSV picker; callers own persistence. */
@OptIn(ExperimentalLayoutApi::class)
@Composable
fun ColorPicker(value: String?, onChange: (String?) -> Unit, defaultColor: Color, modifier: Modifier = Modifier) {
    var custom by rememberSaveable { mutableStateOf(false) }
    Column(modifier, verticalArrangement = Arrangement.spacedBy(8.dp)) {
        FlowRow(horizontalArrangement = Arrangement.spacedBy(6.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
            playerColors.forEach { (name, hex) ->
                val hue = playerColor(hex, defaultColor)
                val picked = value.equals(hex, ignoreCase = true)
                Box(
                    Modifier.size(48.dp)
                        .selectable(picked, role = Role.RadioButton) { onChange(hex) }
                        .semantics { contentDescription = name }
                        .padding(3.dp)
                        .background(hue, CircleShape)
                        .border(if (picked) 3.dp else 1.dp, if (picked) palette.text else palette.lineStrong, CircleShape),
                    contentAlignment = Alignment.Center,
                ) {
                    if (picked) Text("✓", color = colorInk(hue))
                }
            }
        }
        FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            FilterChip(value == null, { onChange(null) }, label = { Text("Default") })
            FilterChip(value != null && playerColors.none { it.second.equals(value, true) }, { custom = true },
                label = { Text("Custom…") }, leadingIcon = {
                    Box(Modifier.size(18.dp).background(playerColor(value, defaultColor), CircleShape)
                        .border(1.dp, palette.lineStrong, CircleShape))
                })
        }
    }
    if (custom) {
        val initial = remember { FloatArray(3).also { android.graphics.Color.colorToHSV(playerColor(value, defaultColor).toArgb(), it) } }
        var hue by rememberSaveable { mutableFloatStateOf(initial[0]) }
        var saturation by rememberSaveable { mutableFloatStateOf(initial[1]) }
        var brightness by rememberSaveable { mutableFloatStateOf(initial[2]) }
        val picked = Color.hsv(hue, saturation, brightness)
        AlertDialog(
            onDismissRequest = { custom = false },
            title = { Text("Your color") },
            text = {
                Column(Modifier.verticalScroll(rememberScrollState())) {
                    Box(Modifier.fillMaxWidth().height(64.dp).background(picked, MaterialTheme.shapes.medium)
                        .border(1.dp, palette.lineStrong, MaterialTheme.shapes.medium))
                    Spacer(Modifier.height(12.dp))
                    ColorSlider("Color", hue, { hue = it }, (0..6).map { Color.hsv(it * 60f, 1f, 1f) }, 0f..360f)
                    ColorSlider("Intensity", saturation, { saturation = it },
                        listOf(Color.hsv(hue, 0f, brightness), Color.hsv(hue, 1f, brightness)))
                    ColorSlider("Brightness", brightness, { brightness = it },
                        listOf(Color.Black, Color.hsv(hue, saturation, 1f)))
                }
            },
            confirmButton = { TextButton({
                onChange(String.format(Locale.ROOT, "#%06X", picked.toArgb() and 0xFFFFFF))
                custom = false
            }) { Text("Use color") } },
            dismissButton = { TextButton({ custom = false }) { Text("Cancel") } },
        )
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun ColorSlider(label: String, value: Float, onChange: (Float) -> Unit,
    colors: List<Color>, range: ClosedFloatingPointRange<Float> = 0f..1f) {
    Text(label)
    Slider(value, onChange, valueRange = range,
        modifier = Modifier.semantics { contentDescription = label },
        colors = SliderDefaults.colors(thumbColor = palette.text),
        track = {
            Box(Modifier.fillMaxWidth().height(16.dp)
                .background(Brush.horizontalGradient(colors), CircleShape)
                .border(1.dp, palette.lineStrong, CircleShape))
        })
}
