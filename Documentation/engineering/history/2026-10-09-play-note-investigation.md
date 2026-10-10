# Play-note investigation, 2026-10-09

Owner: Codex. Branch: `codex/master`. Reviewed baseline: `5e067f7`.
The owner authorized thorough investigation and small, easy fixes. Changes are
staged locally for review; significant features and hardware changes are proposals.

Confidence: source findings below are **Reconstructed**, the browser and host
results are **Verified** within those environments, and physical acceptance is
**Needs verification**. No Atlas, Sigil or Android device is attached.

## 1. Resume during a win claim — small fix implemented

Both `Android/.../ui/tablet/TabletTable.kt` and `Atlas/web/src/tablet.html`
treated every PAUSED state as an ordinary pause. Their central modal covered
the confirming player's existing Confirm/Deny panel. Atlas's
`handleResumeIntent` correctly rejects resume during a win claim or elimination
selection (`Atlas/src/gameplay_intents.cpp`). Bypassing this validation would
break the confirmation flow.

The tablet clients now leave the central overlay closed during a win claim,
so the scheduled player's panel can answer. Ordinary pause retains Resume.
An elimination selection shows an explanation directing the table to Atlas or
the selecting Sigil, with no invalid Resume action. No authority, permission,
Intent or wire change. The Android standalone game uses this same table view;
its ordinary pause remains available because it reports no pending decisions.

**Verified:** real Chromium page, four virtual players, claim then denial,
another claim with three sequential confirmations, portrait panels, ordinary
resume and elimination-selection display. Atlas's host suites also cover the
authoritative tablet controls and win flow. Android compilation and device
interaction remain unverified.

## 2. Seat B after tablet seating — confirmed implementation restriction

`profile_picker.cpp::buildEntries` offers profiles already seated through a
browser/tablet and marks them as attachable. But
`table_intents.cpp::bindPhysicalProfile` explicitly rejects `addsSeatB && joined`
with "That profile is already at the table; leave there first". This affects
already-seated profiles even with four players. The actual table capacity is
16 players (`turnhub_types.h`), not four. An existing host scenario around
`scenarios.cpp`'s Add seat B tests intentionally encodes this rejection.

Simply removing that rejection is unsafe. `Lobby::replaceController` requires
the destination controller to be unseated and replaces an entire controller
with slot A; it cannot move a browser participant to an occupied Sigil's B.
Binding the profile and ignoring a failed replacement could release its browser
handle while losing its live participation. Mid-game attachment is separately
limited to slot A (`attachInGame`), so it also needs explicit seat-level work.

**Proposed:** add an Atlas-owned operation to move an existing participant to
an empty physical seat, validating profile policy, destination vacancy, game
scope and physical confirmation before any mutation. Preserve participant ID,
profile, starter, and, in-game, life/counters/timing and pending references.
Bind/release controllers only after a successful move, with failure rollback.
Shared Sigil seats currently remain adjacent in lobby order; attaching a
nonadjacent tablet player needs an explicit ordering decision, not a silent
promise that arbitrary turn order survives. Cover full capacity, A/B versus
B/A, nonadjacent players, two games, duplicate selection and binding failures.
This is larger than a minor investigation fix.

## 3. Dedicated pass/action button beside the joystick — recommended trial

`Sigil/src/main.cpp` currently reads four virtual direction keys and GPIO32
joystick click, then routes menus and game actions. There is no separate
pass/action GPIO. The click also wakes the device and, while unpaired and
outside the menu, has a long-hold pairing gesture.

**Proposed:** retain the joystick, add one debounced button, and reuse the
existing semantic action path. During ordinary active play it requests Pass
(or cancels its own pending pass); during a required decision it requests only
the explicitly displayed action. Keep win claim/concede holds and menu safety
rules; a new button must not accidentally inherit pairing or destructive holds.
Settle whether it is a dedicated Pass button or a context-sensitive Action
button before PCB work. GPIO25 is described as unused in current Sigil source,
but check carrier access, electrical loading and wake requirements before
assigning it. GPIO13 is already reserved for the front LEDs; BOOT stays Pair.
Test concurrent joystick/button input and held-button wake without a game action.

## 4. Player-facing LEDs — requirement recorded, hardware path exists

The prior design made the front strip conditional on the case. The owner's
new requirement supersedes that condition. `PLANNED_DESIGNS.md` now records
player-facing LEDs as required.

The Rev A carrier already reserves J6 and GPIO13, with its own level shifter
and 330-ohm resistor. The documented candidate is 6–8 SK6812 RGBW pixels.
Production Sigil firmware drives only the GPIO26 Jewel; it does not drive J6.
The OLED PCB remains a placeholder, so its implementation needs the same
player-facing requirement carried into layout.

**Proposed:** derive both outputs from Atlas's existing semantic LED state and
seat colors, preserving text cues, reduced motion and brightness limits. Start
with the same essential turn/decision cues on both sides. Choose pixel count,
case location, connector/cable clearance and player-eye brightness, then measure
combined ring/strip current. No new dependency or wire field is inherently
needed; a genuinely independent cue or user brightness preference may need a
contract/preference extension. Do not freeze the hardware before these checks.

## 5. Atlas instability with >2 Android clients — cause still unproven

Current Atlas is ESP32-WROOM-32E N4, 4 MB flash and no PSRAM. It configures
eight AP stations, so the symptom is not an explicit two-client cap.
Already implemented: 16 KiB loop stack, health/stack logging, cached state
names, change-driven display/LED sends and a separate ESP-NOW transmit task.
Do not present those as new fixes. The documented historical 73-second send
stall and `ESP_ERR_ESPNOW_NO_MEM` are evidence from older playtests, not a
fresh capture of this build.

Remaining source-level candidates, not confirmed causes:

- Synchronous `server.handleClient()` runs in the same loop that services
  gameplay, display, recovery and input. Slow HTTP handling can delay them.
- Android state polling is serialized at about one second, but seat metadata
  refreshes on every revision change and every fifth unchanged poll. Each
  revision change also launches a session refresh from HomeViewModel, which
  can fetch settings too. Those streams/actions have no shared per-device
  request budget. The session mutex serializes its calls but can accumulate
  redundant refresh work when latency rises.
- `ClientState::revision_` is static across tables, so activity at the other
  table can trigger metadata/session refreshes here too.
- The normal portal polls status, seats, session and counters each second,
  plus slower settings/devices requests. The tablet portal is lighter (one
  state request every 800 ms), but action/visibility-triggered calls can overlap
  scheduled polling; it lacks the normal portal's in-flight guard.
- State JSON still allocates a String per response and grows with players and
  nonzero Commander cells. Extra counters/artwork must not unnecessarily
  increase this burden. ESP-NOW has bounded queues (48 TX, 32 RX/events),
  bounded NO_MEM retries and a 250 ms send-completion wait. Queue pressure and
  HTTP/heap behavior need measurement together.

**Recommended bench:** exact flashed SHA/ELF, USB serial plus SD log, heap and
largest-block trend, loop stack minimum, request durations, queue depth/drops,
send errors and pass latency. First reproduce with 2/3/4 Android clients at
unchanged polling; compare Sigil-only and tablet-only runs, then mixed clients,
both tables, join/leave and slower links. Follow C06's longer 4/8-client matrix.
Use firmware-specific panic decoding. Tune only after a baseline capture and
compare one change at a time. A host suite cannot prove radio stability.

### S3 versus second radio

Espressif's [ESP32-S3 datasheet](https://www.espressif.com/sites/default/files/documentation/esp32-s3_datasheet_en.pdf)
was read for this investigation. It specifies dual cores up to 240 MHz, 512 KB
SRAM, a 2.4 GHz 802.11b/g/n radio, and optional flash/PSRAM variants. An S3
is not automatically more internal SRAM or a second Wi-Fi radio. A suitable
board with PSRAM offers room for movable display/application allocations;
Wi-Fi/internal allocations still need their own budget. It does not prove a
fix for radio contention or blocked handlers.

An S3 port needs a specific display/PSRAM board, GPIO remapping, touch/SD/power
validation, partitions and OTA, and a speaker change: current Atlas uses
`I2S_MODE_DAC_BUILT_IN`/GPIO26, which is not an S3 built-in DAC path. It also
needs validation against the pinned Arduino-ESP32 2.0.17 toolchain or a
deliberate toolchain migration. This is a hardware port, not a board-name edit.

A second MCU/radio could isolate ESP-NOW from phone Wi-Fi via a bounded wired
SPI/UART bridge. Atlas must retain game authority; the bridge owns transport
only. Add framing, flow control, sequence/ack handling, restart behavior and
diagnostics. The board has few spare pins because display, touch, card, speaker,
LED and battery functions already consume them. Two nearby 2.4 GHz radios
also need channel/antenna/coexistence testing.

**Recommendation:** measure first. Prefer a PSRAM-equipped S3 evaluation if
internal heap is the limiting evidence and offloadable allocations are large.
Evaluate a radio bridge if failures correlate with Wi-Fi/ESP-NOW pressure
after application load is controlled. Either prototype should meet the same
multi-client acceptance test; neither is currently an established cure.

## 6. Pixel Fold signup/PIN fields — small mitigation implemented

`ui/home/SignInDialog.kt` used a non-scrolling Column inside ModalBottomSheet.
Create-account fields, account lists, confirm-secret fields, remember controls
and actions can exceed the available height on a Fold or with the keyboard.
The sheet now scrolls using the standard Compose scroll state; Material's
existing inset handling remains responsible for keyboard/window insets.

**Needs verification:** this is a source-supported mitigation, not a device
reproduction. Check folded/unfolded, portrait/landscape, keyboard open, large
text, long account lists, field focus, create/sign-in and rotation. Include the
7- and 10-inch tablets. Further bring-into-view work depends on those results.

## 7. Non-turn breathing — deliberate current behavior, decision open

`Sigil/src/sigil_led.cpp` sets Waiting to a constant level of 64/255. It
already renders the seat's custom color (or the theme's calm default), including
separate A/B halves. YourTurn breathes for A and double-pulses for shared B.
The absence of waiting breathing is therefore not a broken animation.

**Recommendation:** allow a gentle optional waiting breathe in the custom
seat color, with a dimmer/narrower range and longer period than the turn cue.
Keep reduced-motion waiting steady, preserve monochrome-safe differences and
give turn/decision/timer overlays priority. A preference must be Atlas-owned;
adding a setting requires the accessibility/preference and radio contracts to
be designed. The owner has not yet chosen this behavior; no LED code changed.

## 8. Free a Sigil after all its players are eliminated — needs detachment

Elimination preserves the game's player/controller records for results and
rematch. `sigil_menu.cpp::canTakeOverPlayer` refuses a controller already in
the game, `attachInGame` refuses an occupied controller, and `ChooseTable`
refuses one still in a game. The restriction remains even when all its seats
are eliminated. Unpairing hardware is a separate lifecycle operation and
does not provide identity-preserving game detachment.

**Proposed:** explicit Release controller, validated by Atlas only when every
seat on it is eliminated and no decision/entry depends on it. Preserve each
eliminated participant/profile, life/counters and stats in the original match,
move its live controller references to virtual/detached seats, release the
physical A/B bindings and then expose the normal attach picker. Preserve the
old game's recovery and define rematch behavior when hardware is now used
elsewhere. Cover one survivor refusing release, shared seats, two games,
reboot and no double statistics. Reuse seat-level reassignment work from #2.

## 9. Poison, commander tax and other counters — cross-layer feature

Connected GameEngine currently has life and source-specific Commander damage;
`ChangeCounter` means Commander damage, not a generic named counter. HTTP
state/schema, Android models and the standalone engine likewise lack the
requested general counters. A UI-only addition would lose canonical/recovery
state and split behavior between clients.

**Proposed:** a small bounded counter vocabulary (poison, commander casts/tax,
energy, experience; settle the initial list). Add Atlas-owned per-participant
values and one validated semantic counter mutation; persist in match recovery,
reset on new matches and render/edit through tablet, web and app. Mirror the
separate state in Android standalone play and local match records. Extend
contracts and signed-size/recovery tests deliberately.

Poison is bookkeeping and must not auto-eliminate. Commander tax should
distinguish commander 1/2 and preferably track command-zone cast count, with
the standard +2-per-prior-cast display and manual correction; settle whether
the owner instead wants an editable mana-tax total. It is not Commander
damage and must not change life. Decide bounds, team ownership and which
counters count as "received" in statistics; tax is not naturally a received
counter. Preserve labeled text, large touch targets and no new dependencies.

## 10. Modified order / gift an extra turn — scheduled turns, not seat moves

`Lobby::moveSeat` is lobby-only. `GameEngine::passTurn` selects the next living
player/team in circular order; neither engine has an extra-turn schedule.
Moving seats or force-passing repeatedly would distort timers/statistics and
permanent order.

**Proposed:** an Atlas-owned bounded queue of temporary turns, separate from
base order, with explicit Grant/cancel extra turn Intents validated for the
current table. On pass, consume the appropriate scheduled turn, then resume
at the correct base-order cursor. Record completed turns and time normally;
do not mutate participant numbers. Capture the queue/cursor in recovery and
clear at new games. Display who goes next and why on all clients; the Sigil's
next-player display/protocol needs review. Standalone needs equivalent local
behavior under its existing separate-game exception.

Open decisions: who may grant/cancel, immediate versus after-a-specified-turn
insertion, multiple-grant order (Magic's most recently created extra turn goes
first), self turns, skipped turns and team turns. Test eliminated recipients,
multiple/self grants, pass grace/cancellation, pause, claims and reboot before
implementation. No new dependency; edits need accessible confirmation/undo.

## Shared-screen win-claim UX follow-up

Fix #1 restores the existing flow for players with no other input device;
the browser regression exercises exactly that case. Beyond this fix, show the
claimant and responding player, progress through the remaining confirmations,
and a waiting message on other panels. Prevent drawers/life controls from
competing with decisions, keep the prompt facing the responder, and test teams,
shared Sigils, 2–8 panels and disconnected sessions. Preserve sequential Atlas
confirmation and explicit denial. Consider a prominent claim action with its
existing confirmation gesture instead of hiding it exclusively in More.

## Validation and next work

### Owner follow-up: router-style web administration

The owner is considering retiring browser gameplay and concentrating on the
Android app (estimated >90% of expected use; not measured telemetry). This is
a proposed product direction, not authorization to delete the current portal.
Whether the shared `/tablet` browser page retires too is being clarified.

Follow-up decision, owner 2026-10-09: web administration and app play are now
the planned direction, including shared tablet play. The completed
[portal/app parity audit](2026-10-09-portal-app-parity.md) identifies migration
gaps and supersedes the provisional scope below; no pages have been removed.

**Recommended boundary:** web administration for Atlas setup, network/device
settings, account permissions, signed firmware/portal updates and diagnostics;
Android for personal play, shared tablet play, counters, win claims, seating
and history. Retain Atlas as game authority and preserve the HTTP API used by
Android. `/api/v1/state`, session/profile endpoints, `/api/control/*` and
`/api/tablet/*` are also app transports, despite their names and source files.
Do not remove them when deleting browser buttons. API authentication and
per-operation authorization must remain; absence of browser UI is not an
app-only security boundary.

Scope of a deliberate retirement slice:

1. Inventory app parity for seating A/B, turn order, claims, moderation,
   counters/history and controller management. Complete blocking app flows
   before removing their only usable client path.
2. Settle first-Admin bootstrap and recovery. A fresh/factory-reset Atlas has
   no Admin, so a blanket Admin gate needs a limited setup exception or an
   app/touchscreen bootstrap. Preserve sign-in for existing administrators.
   Diagnostics currently requires Developer; explicitly decide its portal
   visibility without silently broadening API permissions.
3. Replace the full SD portal and the flash basic fallback with the same
   administration scope. Retire browser game/player controls, stats/profile
   play pages, and `/tablet` if chosen. Update navigation, root/QR landing,
   signed portal pack contents/version and remove obsolete browser assets,
   strings and smoke tests together. Existing installed/cached packs need
   a cutover check so old gameplay pages do not persist by accident.
4. Preserve an administration/update path when the card or app is unavailable.
   Native play means an iPhone/browser-only player loses personal web control;
   a shared Android tablet and physical Sigils remain alternatives. Decide
   that product tradeoff explicitly.
5. Validate authorization on every retained settings action, initial setup,
   cardless fallback, firmware recovery and all app game APIs. Rewrite the
   manual and references around the final boundary.

This reduces duplicated UI, assets and browser polling, but Android polling and
the shared radio are still present. It is not itself proof of a multi-Android
stability fix. Prioritize app counters and win UX; defer new browser gameplay
features while this direction is reviewed. The small staged portal overlay fix
and its regression remain useful evidence for current firmware and can be
removed with the retired page in the cutover slice.

- Atlas application/storage/profile-store/OTA host suites: passed with
  ASan/UBSan, `ASAN_OPTIONS=detect_leaks=0` (workspace tracing prevents LSan).
- Client contracts: passed, 10 generated HTTP/projection responses and
  10 shared fixtures. Adapter audit: passed, 31 adapters.
- Real tablet browser regression: passed using local Chromium and Playwright.
  Run `node Atlas/tests/host/tablet_smoke.cjs`; optionally set
  `PLAYWRIGHT_EXECUTABLE_PATH` or `PLAYWRIGHT_CHANNEL` for the installed browser.
- Portal pack check: passed (1.3.3, 20 files). Whitespace check: passed.
- Android build attempt blocked before compilation: Gradle distribution is
  not cached and its default lock path is outside writable workspace roots;
  no configured Android SDK is available. No Android build/test pass claimed.
- PlatformIO unavailable; no new firmware code was changed. No firmware
  build, flashing, hardware stability or physical layout/LED test claimed.

Recommended order: review the minor UI fixes; implement/test seat-level B
attachment; capture Atlas stability while device-checking Fold/win flows;
reuse seat reassignment for eliminated-controller release; settle button/front
LED hardware together; then counters and temporary-turn scheduling.
