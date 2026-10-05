# Playtest: shared Sigil turn order (2026-10-05)

The Atlas order controls moved controllers rather than seats. In a four-player
table whose last Sigil held players 3 (A) and 4 (B), moving player 3 later was
rejected as already last. Seat B also needed a left/right choice to allow B/A
turn order.

## Feature gate

- Canonical state: Atlas `Lobby`, with an A/B order for each shared Sigil.
- Intents: `MoveSeat` swaps the pair when moving toward the other local seat;
  outside that pair it moves the whole controller. `SetSeatSide` explicitly
  chooses B before A (left) or B after A (right).
- Validator: the table intent handlers allow Atlas touchscreen requests in the
  lobby only, validate the target seat and require B for a side setting.
- Persistence: lobby RAM, retained for a rematch. The existing Atlas match
  checkpoint records the ordered physical seats; recovery accepts adjacent
  A/B and B/A pairs and reconstructs their order. No new NVS preference.
- Rendering: Atlas shows the player's actual position and the B side/order;
  Sigils render names, seat labels and avatars using physical seat identity.
  HTTP clients continue to receive ordered players and their physical slots.
- Shared contract: `DISPLAY_FLAG_PRIMARY_B` uses the retired host bit `0x10`.
  Atlas and Sigil firmware must be updated together; packet sizes stay the same.

## Controls and behavior

In the lobby, tap either player chip on a shared Sigil. Earlier/Later can swap
A/B. B left selects B before A; B right selects B after A. Both players remain
adjacent when their whole Sigil moves. The selected starter, participants,
profiles and seat bindings stay attached to their physical seats. Changing
order cancels a pending start arm. Removing B or emptying the table clears
that Sigil's side selection.

## Verification

Host scenarios exercise player 3 moving later in a four-player game, boundaries,
left/right controls, starter/participant preservation, unauthorized and invalid
requests, match turn progression, reversed-pair checkpoint restoration and
nonadjacent-pair rejection. OLED tests cover physical A with the higher player
number and B with the lower one. Hardware play testing is still required.
