package com.turnhub.android.ui.components

import androidx.compose.material3.ListItem
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import com.turnhub.android.domain.PhysicalSigilAtTable
import com.turnhub.android.domain.seatLabel

/**
 * One physical Sigil seated at the current table and the seats it holds.
 * Deliberately shows no connectivity or friendly device name: Atlas's state
 * snapshot reports neither, only the controller handle and its seats.
 */
@Composable
fun PhysicalSigilRow(
    sigil: PhysicalSigilAtTable,
    modifier: Modifier = Modifier,
) {
    ListItem(
        modifier = modifier,
        headlineContent = { Text("Sigil ${sigil.controller.id}") },
        supportingContent = {
            Text(
                sigil.seats.joinToString(" • ") { seat ->
                    "Seat ${seatLabel(seat.slot)}: #${seat.playerNumber} ${seat.label}"
                },
            )
        },
    )
}
