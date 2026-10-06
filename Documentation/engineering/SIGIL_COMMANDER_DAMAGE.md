# Commander damage on a Sigil

## Feature gate

1. Atlas `GameEngine` owns received damage, life and the last-hit undo receipt
   for each recipient. The Atlas Commander picker owns disposable entry state.
2. `RecordCommanderHit` and `UndoCommanderHit` apply the change. Browsing sends
   `CommanderKey` messages tagged with the current page revision.
3. The counter intent handler requires a living recipient in a running or
   paused Commander match without a pending table decision, verifies seat
   ownership and validates the source, commander, amount and limits.
4. Existing Atlas match checkpoints persist the resulting life and damage
   totals. Entry pages and undo receipts are RAM-only and clear on recovery,
   rematch or reset. There is no new NVS preference or recovery schema.
5. Sigil OLED and e-ink render the Atlas page. They neither apply hits nor
   infer seat A/B from player number. Browser/app state sees the same totals.
6. The shared radio contract adds `CommanderFlow = 50` (80-byte page) and
   `CommanderKey = 51`, plus menu actions 24 (Commander damage), 25 (Undo
   hit), 26 (Add partner) and 27 (Drop partner). `MenuState2` now carries 29 action bits and a 3-bit revision; its
   unused default-action field is derived locally instead. `SelectAction`
   bits 11–15 carry the receiving player shown when the action was chosen.
   Atlas and Sigil firmware must be rebuilt together. The retired hardware
   harness is unsupported and excluded from builds.

## Controls

Both displays (Sigil 0.9.10): Up opens **Menu**, which holds **Cmd damage**,
**Undo hit** (when available) and **Partner: off/on** in a Commander game. See
`Sigil/DISPLAY.md`, Menus. The click still passes, Left/Right still adjust life
and Down switches shared seats.

**Partner commanders (2026-10-06).** Off for every player by default. A player
turns them on for their own shown seat from Menu (Partner: off chooses on).
Atlas owns the flag per player per match (`GameEngine::setPartner`, Intent
`SetPartner`); a rematch or reset clears it. Once a second commander has dealt
damage it stays on. It is not saved separately: recorded commander-2 damage
turns it back on after recovery. A Sigil hit from commander 2 is refused
without partners; damage entered for a second commander from a phone turns
partners on for that player.

The receiver is the seat shown when opening the flow. Its name and physical
A/B slot remain on every page, even if turns advance while the page is open.

- Choose the attacking player with Left/Right, then click. The list skips the
  receiver and eliminated players; record self-damage from a phone.
- Only when the attacker has partners: choose Commander 1 or 2 with
  Left/Right, then click. Otherwise this step is skipped (commander 1).
- Set damage with Left/Right; hold to repeat. Click opens a preview.
- Review the source, commander, amount, life change and commander-damage total.
  Click applies both changes together. Up goes back; Down cancels at any step.
- A result page reports success or rejection; click closes it.

Undo previews the last hit recorded through this flow for the selected receiver,
then reverses its damage and life changes together when confirmed. It is one-shot.
A correction to that same damage cell invalidates the receipt. Changing ordinary
life or another damage cell does not remove the receipt: undo reverses only the
recorded hit. After a reboot, totals recover but Undo is unavailable.

Nothing applies while browsing. Entry amounts range from 1 to 9999; the engine's
existing total/life bounds still apply. A fresh confirmation is required if life
or damage changed since the preview. Stale or duplicate keys cannot reapply a hit.
Sigil confirmation keys require the latest preview to have finished drawing,
including on slow e-ink. Once the Amount stage is drawn, Left/Right taps go
through without waiting for each e-ink refresh; partial refresh stays off
because the panel's built-in partial waveform fades the image. Pending ordinary life edits are sent before opening the
flow. No table pause is required. Idle (60 seconds), disconnect, elimination,
a pending table decision, match end or a new match closes the entry.

This records totals; it does not add automatic elimination at 21 commander damage.

## Verification

Atlas host scenarios cover the real menu/input adapters and dispatcher, shared
seat B targeting, source and partner selection, atomic apply and undo, unchanged
totals until confirmation, duplicate/stale keys, changed previews, ownership and
limits, cancellation, paused games, idle/disconnect, elimination, new matches
and table-decision closure. Sigil host tests
cover menu access and previews at the real OLED dimensions. Screen previews use
both production display renderers for every entry stage.

Hardware acceptance: enter and undo hits on OLED and e-ink, including both seats
on a shared Sigil, hold an amount key, change turns while entering, and reconnect.
The local firmware build was blocked by the network proxy denying PlatformIO’s
package registry. The existing GitHub CI workflow builds all firmware targets
and uploads binaries and screen previews for review. Versions for this change:
Atlas 0.6.7-dev and Sigil 0.9.9-dev.

Sigil 0.9.9 never showed the flow: its radio receive filter
(`received_packet.h`) had no entry for the sealed Commander page, so every
page was dropped before it was opened. Fixed in Sigil 0.9.10; a host check in
`secure_link_scenarios.cpp` now covers every sealed page size.
