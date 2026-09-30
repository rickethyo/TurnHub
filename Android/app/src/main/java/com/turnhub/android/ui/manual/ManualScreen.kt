package com.turnhub.android.ui.manual

import androidx.activity.compose.BackHandler
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.statusBarsPadding
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.AnnotatedString
import androidx.compose.ui.text.SpanStyle
import androidx.compose.ui.text.buildAnnotatedString
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.turnhub.android.domain.UserManual
import com.turnhub.android.domain.UserManual.Block
import com.turnhub.android.ui.components.BrassCard
import com.turnhub.android.ui.components.Eyebrow
import com.turnhub.android.ui.components.ToneButton
import com.turnhub.android.ui.components.tableBackground
import com.turnhub.android.ui.theme.palette
import kotlinx.coroutines.launch

/** Asset generated from the newest Documentation/User Manual .docx. */
private const val MANUAL_ASSET = "manual.md"

/**
 * The user manual, bundled with the app so it reads offline and without an
 * Atlas. A contents card jumps to each chapter; system Back or Close returns.
 */
@Composable
fun ManualScreen(onClose: () -> Unit, modifier: Modifier = Modifier) {
    BackHandler(onBack = onClose)
    val p = palette
    val context = LocalContext.current
    val manual = remember {
        runCatching { context.assets.open(MANUAL_ASSET).bufferedReader().use { it.readText() } }
            .map(UserManual::parse)
            .getOrNull()
    }
    val listState = rememberLazyListState()
    val scope = rememberCoroutineScope()
    // Items before the manual's blocks: the header card and the contents card.
    val leadingItems = 2

    Column(modifier.fillMaxSize().tableBackground(p)) {
        Row(
            Modifier
                .fillMaxWidth()
                .background(p.bg.copy(alpha = .92f))
                .statusBarsPadding()
                .padding(horizontal = 16.dp, vertical = 10.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Text(
                "User manual",
                color = p.text,
                style = MaterialTheme.typography.headlineSmall,
                modifier = Modifier.weight(1f).semantics { heading() },
            )
            ToneButton("Close", onClose)
        }
        Box(Modifier.fillMaxSize(), contentAlignment = Alignment.TopCenter) {
            if (manual == null) {
                Text(
                    "The manual could not be opened.",
                    color = p.bad,
                    modifier = Modifier.padding(24.dp),
                )
            } else LazyColumn(
                state = listState,
                modifier = Modifier.widthIn(max = 720.dp).fillMaxWidth().navigationBarsPadding(),
                contentPadding = PaddingValues(horizontal = 16.dp, vertical = 12.dp),
                verticalArrangement = Arrangement.spacedBy(8.dp),
            ) {
                item {
                    Column(Modifier.padding(bottom = 4.dp)) {
                        Text(manual.title, color = p.text, style = MaterialTheme.typography.headlineMedium)
                        manual.edition?.let { Text(it, color = p.muted, style = MaterialTheme.typography.titleSmall) }
                    }
                }
                item {
                    BrassCard(contentPadding = PaddingValues(vertical = 12.dp, horizontal = 8.dp)) {
                        Eyebrow("Contents", Modifier.padding(horizontal = 10.dp))
                        Column {
                            manual.chapters.forEach { (index, title) ->
                                Text(
                                    title,
                                    color = p.text,
                                    style = MaterialTheme.typography.bodyLarge,
                                    modifier = Modifier
                                        .fillMaxWidth()
                                        .heightIn(min = 48.dp)
                                        .clickable(role = Role.Button, onClickLabel = "Go to chapter") {
                                            scope.launch { listState.animateScrollToItem(leadingItems + index) }
                                        }
                                        .padding(horizontal = 10.dp, vertical = 12.dp),
                                )
                            }
                        }
                    }
                }
                items(manual.blocks) { block -> ManualBlock(block) }
            }
        }
    }
}

@Composable
private fun ManualBlock(block: Block) {
    val p = palette
    val type = MaterialTheme.typography
    when (block) {
        is Block.Heading -> Text(
            block.text,
            color = if (block.level == 1) (if (p.dark) p.accentHi else p.accent) else p.text,
            style = when (block.level) {
                1 -> type.headlineSmall
                2 -> type.titleLarge
                else -> type.titleMedium
            },
            modifier = Modifier
                .padding(top = if (block.level == 1) 20.dp else 10.dp)
                .semantics { heading() },
        )
        is Block.Paragraph -> Text(block.text.annotated(), color = p.text, style = type.bodyLarge)
        is Block.Bullet -> ListItem("•", block.text)
        is Block.Step -> ListItem("${block.number}.", block.text)
    }
}

@Composable
private fun ListItem(marker: String, text: UserManual.Rich) {
    val p = palette
    Row(Modifier.padding(start = 8.dp), horizontalArrangement = Arrangement.spacedBy(10.dp)) {
        Text(marker, color = p.muted, style = MaterialTheme.typography.bodyLarge, modifier = Modifier.widthIn(min = 18.dp))
        Text(text.annotated(), color = p.text, style = MaterialTheme.typography.bodyLarge)
    }
}

private fun UserManual.Rich.annotated(): AnnotatedString = buildAnnotatedString {
    append(text)
    bold.forEach { addStyle(SpanStyle(fontWeight = FontWeight.Bold), it.first, it.last + 1) }
}
