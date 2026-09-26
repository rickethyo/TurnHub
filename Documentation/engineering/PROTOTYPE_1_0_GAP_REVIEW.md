# Prototype 1.0 gap review (2026-09-26)

A check of the current tree against the
[Prototype 1.0 field-test priority lane](STAGED_CHANGES.md#prototype-10-field-test-priority-lane).
The goal of that lane is a build another person can use **without a development
computer**. Confidence labels follow the engineering README: host tests passing is
not hardware acceptance.

## Summary

The software side of the lane is largely built. Very little of it has been accepted
on hardware. Since 2026-09-24 (58 commits in three days) the work drifted into new
hardware and features: the Atlas touchscreen, the Jewel ring, the joystick and OLED
Sigils, microSD statistics and diagnostics, the Android app, the test harness,
avatars and table-clock sync. Several of those help the field test, but they also
broke lane item 2, the feature freeze, and added bench checks that nobody has run.

**Recommendation:** freeze features now. Spend the next sessions on bench acceptance
(items 3, 5 and 6 below) and on the first carrier PCB, which is what makes three
identical field-test Sigils practical.

## Lane items

| # | Lane item | Status | Evidence / gap |
|---|---|---|---|
| 1 | Physical profile selection, reusable Sigils | *Partially implemented* | Seat-A picker on e-ink and OLED Sigils (host-tested). Selectable startup, seat B and duplicate-name labels remain; see [PHYSICAL_PROFILE_SELECTION.md](PHYSICAL_PROFILE_SELECTION.md). Hardware: *Needs verification*. |
| 2 | Freeze major portal features | **Not held** | Since the lane was written: avatars, a Personalization card, Admin/GM/Developer tabs in Android, touchscreen table actions and more. Needs an explicit freeze from here on. |
| 3 | Interrupted-match recovery | *Implemented, host-tested* | Wired into `setup()`/dispatcher observer on 2026-09-23. Discard is the 5 s End match hold. The abrupt-power test matrix (backlog lines 109-118) is still open; those checkboxes also need updating to match what was built. |
| 4 | Auxiliary button path | *Superseded* | GPIO32 now carries the joystick click (e-ink) or the Select key (OLED); Pause/Win became menu actions. The backlog's "revisit Action long-press" item can be closed or restated. |
| 5 | Pairing state machine | *Implemented* | 15 s manual pairing, Pair = DevKit BOOT button. Radio bench acceptance and a forget-device flow remain. |
| 6 | Three-Sigil field-test set | **Not started as a set** | Sigils are breadboards of two different kinds (e-ink/joystick and OLED/buttons). No repeated cold-boot / power-loss / reconnect / re-pair runs are recorded. The carrier PCB (below) is the blocker for building three alike. |
| 7 | Out-of-box setup | *Mostly implemented* | Atlas screen shows QR codes, access codes replace the admin hold, Android joins the Atlas Wi-Fi itself, factory reset exists on both devices. Not yet tried end to end by someone other than the owner. |

## Added since the lane was written (not in Prototype 1.0 scope)

- **Hardware:** Atlas moved to the LCDwiki E32R28T touchscreen board with an on-board
  speaker; Sigils gained the NeoPixel Jewel 7 ring, the analog joystick (e-ink) and
  five pushbuttons (OLED), and a GPIO4 display-type strap.
- **Features:** Commander damage and life approvals, game profiles, accounts and
  moderation, microSD statistics/diagnostics, preset avatars, table-clock light sync,
  TestHarness 0.8.0, the Android client with Settings/Dev tabs.
- **Useful for the field test:** the touchscreen (no phone needed to run a table),
  the test harness (repeatable games without people), factory reset and QR setup.
- **Cost:** each one adds bench checks. The verification backlog has 77 open items,
  many from before this lane; the ones that block Prototype 1.0 are the recovery
  matrix, radio pairing acceptance, profile persistence across an update, the
  Sigil GPIO/breadboard check and Sigil current draw.

## Hardware path: the e-ink Sigil carrier

The e-ink Sigil stack is complete for the first prototype: DevKit, e-paper board,
joystick, Jewel ring and buzzer. As of 2026-09-26 the Rev A schematic
([KiCad/PCB/Sigilv1](../../KiCad/PCB/Sigilv1/README.md)) has through-hole footprints
for every carrier part, the ring's bulk capacitor, +3V3 decoupling and the GPIO4
strap. Before a board can be ordered:

Update the same day: U1 has a footprint from the published Inland DevKit
dimensions (rows 25.4 mm apart), the buzzer is a passive piezo driven directly, and
a 74AHCT1G125 level shifter (U2) was added for the ring. The Jewel moved to a
small adapter board with a JST-XH pigtail into the Sigil board. What's left:

1. **Caliper-check the DevKit** row spacing and outline, and test-fit the sockets.
   Print both footprints 1:1 and test-fit the DevKit and a Jewel.
2. Re-run KiCad ERC, then lay out the board.

A second Jewel strip is optional and not drawn; GPIO26's single data line can chain
more pixels from J5's DOUT if it is ever wanted, within the USB current budget.
