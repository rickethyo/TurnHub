# TurnHub Staged Changes

Current pairing update (2026-09-22): physical buttons are owner-verified and manual
15-second pairing with persistent MAC associations is now implemented. The boot
pairing fallback and visual mock are superseded. Radio bench acceptance and
forget-device management remain pending. See [Manual Pairing](MANUAL_PAIRING.md).


Owner decisions and follow-through (2026-09-24, later the same day):

- **End a match as a draw:** implemented. Holding the Atlas master button for 5 s
  during a running or paused match (including a recovered one) sends `EndMatch`,
  which ends it with no winner and records a Draw for every player. This is the
  "Discard" path for a recovered match that item 3 below lacked.
- **Forget pairings:** implemented. A 10 s Sigil Pair hold erases the Sigil's
  pairing; admins forget one or all Sigils in Device Settings, and Atlas sends
  `Unpair`. See [Manual Pairing](MANUAL_PAIRING.md#forgetting-a-pairing-2026-09-24).
- **Pairing window:** Atlas's window is admin-adjustable (15/30/60 s).
- **On hold:** the physical profile picker (waits for a D-pad on the Sigil), LED
  brightness and buzzer volume (current hardware cannot vary them).
- **Not planned on e-ink:** the turn timer on the Sigil screen; it will be tried
  on an LCD Sigil model.
- **Long-term goal:** game-scoped statistics and session history (sections 3-5).

All of the above: host scenarios, the portal browser smoke, and PlatformIO
builds of Atlas, `sigil` and `sigil-wokwi` pass (Sigil firmware 0.5.5-dev). The
Android "Draw" label has a unit test that was not run. Nothing was flashed;
hardware acceptance is pending.

Turn timer and cue layers (2026-09-24): the Atlas-owned turn timer, LED cue
profile, audio cue profile and Android player controls are implemented locally with
host/Android tests and an Atlas build; hardware acceptance is pending. See
[Turn timer and cues](TURN_TIMER_AND_CUES.md). Staged follow-ups:

- Timer on the Sigil screen: not on e-ink (owner decision); revisit on an LCD Sigil.
- Route Sigil-local Pairing/Disconnected/Error LEDs through a shared cue profile
  (and so through the player's light style).

Sigil accessibility preferences (2026-09-24): the owner chose per-player settings
that follow the profile. Sigil sound, light style (Standard, Reduced motion,
Monochrome-safe) and adjustable Action hold times are implemented with host,
browser and Android tests and firmware builds (Atlas, Sigil 0.5.4); `ActionRequired`
now sounds for win confirmations and life-change recipients. Hardware acceptance
is pending: see the bench list in [Accessibility](ACCESSIBILITY.md#implemented-accessibility-settings).
Remaining accessibility items are listed there under "Not yet implemented".

Atlas display board (2026-09-24): Atlas now targets the LCDwiki E32R28T 2.8"
ESP32-32E display module (see [Hardware reference](HARDWARE_REFERENCE.md#generation-2-development-atlas)).
Implemented and built, but not flashed: the new pin map, the splash screen,
BOOT (IO0) as the master button (presence check, PASS on release, 5 s hold
to end a match as a draw), and the `min_spiffs` partition table. The Pair button
and the status/Pair LEDs were dropped. The touchscreen (host-tested, built, not
flashed) now replaces them: Pair a Sigil, Pass, Pause/Resume, a hold to end the
match as a draw, and an on-screen countdown for the BOOT hold (see
[Hardware reference](HARDWARE_REFERENCE.md#atlas-touchscreen)). Staged follow-ups:

- **Bench acceptance:** screen orientation, touch calibration, and each touch
  button against a real table.
- **microSD storage:** move bulk records (profiles, statistics, possibly
  logs) from NVS to the card and keep NVS for small or critical settings. Before
  starting, run the feature gate: persistence owner, a card-missing/corrupt
  fail-safe (gameplay must never require the card), migration from NVS, atomic
  writes, PIN-hash exposure on a removable card, and host-test storage stubs.
- Player names on the status screen; more touch actions (Start, Rematch,
  starter selection) once the owner decides which host-only actions the table
  device may take; the on-board RGB LED and speaker as cue outputs; a battery
  gauge.
- Update the user manual: the master button is now BOOT, and pairing moves
  to the touchscreen.

This is the durable staging document for agreed work that has not yet been implemented or fully verified.

Use this file instead of chat history for near-term changes. Keep it concise. Once an item is implemented and verified, move any lasting architectural facts into the appropriate reference document and remove it from here.

## Current baseline

Active baseline: `master` (Atlas ESP32 migration merged in `6844f94`).

The owner confirmed the existing repository build and installed hardware on
2026-09-20. New changes still require their own applicable verification.

Current product rule: Atlas is authoritative for canonical game/table state. Physical Sigils, virtual Sigils, browsers, simulators, and future apps request semantic actions through the shared Intent boundary and render Atlas state.

Accessibility is a hard product requirement. User-facing features must follow `ACCESSIBILITY.md`; essential information or actions must not depend on a single sensory cue or input method when a practical alternative exists.

## Prototype 1.0 field-test priority lane

Prototype 1.0 is the near-term field-test build intended to leave the development
bench and be usable by another person without a development computer. It does not
need production-finished enclosures or batteries. USB power from known-good power
banks is acceptable; setup reliability and recovery are higher priorities.

Near-term order:

Play-test priority update (2026-09-21): Michael requested game profiles/life totals.
The first local implementation is complete with automated checks and a firmware
build; see [Game profiles and life counters](GAME_PROFILES_AND_LIFE.md). Hardware
acceptance and game-scoped statistics remain. Commander damage and cross-player
life approvals were added locally on 2026-09-22, with native/browser checks and
an Atlas build; their own hardware acceptance is pending. See
[Life approval and Commander damage](LIFE_APPROVAL_AND_COMMANDER.md) and the
[Manual V0.2 review](MANUAL_V02_REVIEW.md) for the remaining manual gaps.
Phone-only resets were reported twice; investigation is paused pending serial data.
The subsequently supplied capture ended in a user-confirmed manual reset, so it
does not diagnose those earlier reports.

Local account/portal slice: initial Admin setup, independent Admin/Game Master/
Developer permissions, configurable disconnect actions and private persistent
counts are implemented. See [Accounts and moderation](ACCOUNTS_AND_MODERATION.md).
Hardware acceptance and enforcement of saved mute preferences when nudges are
implemented remain; no account reset or firmware flash has been performed.

1. **Complete physical profile selection/reusable Sigils.** Preserve duplicate-profile
   protection while separating saved last-used preference from live assignment.
2. **Freeze major web-portal features.** After profile selection, only first-run/setup,
   recovery, accessibility, and blocker fixes should displace hardening work before
   the field-test handoff.
3. **Add interrupted-match recovery foundation.** Atlas should persist a compact,
   versioned recovery record after accepted semantic transitions rather than timer
   ticks. A reboot that finds an unfinished valid match should offer Resume or
   Discard. Resume restores the match paused; downtime is never charged to a player.
   Browser sessions still reauthenticate. Physical/browser controllers reattach to
   Atlas-owned participants rather than becoming identity authority. Transient input
   gestures, countdown animation state, button-down state, nudges, and other
   presentation/adaptor state do not survive reboot. Introduce a durable MatchId and
   completion receipt/marker before replaying completion so statistics cannot be
   counted twice after an uncertain write or power loss.

   Audit update (2026-09-23, Claude): the codec/store/checkpoint files
   (`game_checkpoint.*`, `game_recovery.*`, `nvs_blob_store.*`) were already
   sound, but `beginGameRecovery()`/`checkpointGame()` were never called from
   `main.cpp` -- this is the actual reason hardware testing saw no restore
   after power loss, not a bug in the recovery logic itself (matches the
   "retained without activation" note in `STABILIZATION_2026_09_22.md`). Now
   wired: `setup()` calls `beginGameRecovery()` before the portal/radio
   boundary opens and sets `hubState` from the result (`Paused` or
   `GameOver`, matching what was actually saved); `checkpointGame()` is
   called from the dispatcher's existing intent observer (persists after
   every accepted semantic transition, a no-op on rejection) and once a
   second from `loop()` (covers the periodic elapsed-clock case with no
   intents in between). Diagnostic `ATLAS|RECOVERY|...` serial lines cover
   open, read, decode, validate and write outcomes, distinguishing
   NotFound/Corrupt/UnsupportedSchema/IoError from a clean restore. A
   `gameRecoveryLifecycle` host scenario (`Atlas/tests/host/scenarios.cpp`)
   now exercises the real entry points end to end: first-boot `NotFound`,
   checkpoint-after-intent with no direct test call to `checkpointGame()`,
   a simulated reboot restoring paused with downtime excluded and no
   statistics replay, and a corrupted record failing safe. All Windows host
   scenarios pass (verified via an equivalent Linux/g++ build here; PlatformIO
   firmware and hardware acceptance still need the owner's bench).

   Update (2026-09-24): the owner chose a 5-second Atlas master-button hold that
   ends the match as a draw (`EndMatch`). That is the Discard path: it works on a
   restored (paused) match and records a Draw, not a discarded game. Resume
   remains the ordinary Pause/Resume control. The history below is kept for
   context.

   Earlier note: there was no Resume/Discard *decision* UI.
   "Resume" needs no new code -- a restored match lands in the existing
   `Paused` state, so the current Pause/Resume controls (physical and
   browser) already continue it. "Discard" has nothing to hook into yet:
   `ResetGame` is intentionally only reachable from `GameOver` or an
   unstarted `Lobby` today, specifically so it can't be used to nuke an
   ordinary in-progress paused game, and a freshly-recovered match is
   indistinguishable from that once `hubState` is `Paused`. The master-button
   hold answers this without a new `HubState`: it needs someone at the table and
   applies to any match, recovered or not.
4. **Auxiliary button software path.** Done (2026-09-24): the Pause / Win button
   (Sigil GPIO32) sends the existing Action-long and Action-win semantics, so it
   adds no button-owned game rules. Physical operation awaits a bench check; see
   [Hardware reference](HARDWARE_REFERENCE.md#auxiliary-control-revision).
5. **Real pairing state machine.** Done (2026-09-22): manual 15-second pairing on
   both devices with persistent MAC associations replaced passive discovery; the
   Pair button (Sigil GPIO19) is wired, so the temporary boot trigger was never
   needed. Radio bench acceptance and a forget-device flow remain; see
   [Manual Pairing](MANUAL_PAIRING.md).
6. **Build and harden the three-Sigil field-test set.** Exercise cold boot, power loss,
   reconnect, profile persistence, mixed phone/Sigil control, pairing/re-pairing,
   recovery/discard, repeated games, and failure paths. Enclosures may be rough but
   must protect wiring and expose required controls/connectors.
7. **Prioritize out-of-box setup.** A tester should be able to power Atlas/Sigils,
   join the local network, open the portal (preferably by QR), pair/assign controllers,
   log in/create profiles, and start a game without PlatformIO, serial, SSH, or direct
   developer intervention.

Prototype 1.0 recovery is an attempt toward resilient session resume, not a claim
of production-grade crash consistency. If recovery validation fails, Atlas must
fail safe to a fresh lobby while preserving profiles/statistics rather than loading
ambiguous or corrupt game state.

## Staged implementation order

Identity/storage foundation precedes the remaining sequence below. The contract
and first NVS-backed statistics boundary are now implemented locally; see
[Identity and storage contracts](IDENTITY_AND_STORAGE.md). Existing IDs, keys and
v1 statistics format are preserved. New match/controller identity lifecycles,
schema migration and expanded-storage support remain future work.

Automated verification passed: Atlas firmware build, six native gameplay groups,
identity/storage fault scenarios and adapter audit. Pending acceptance: update
without erasing NVS, check an existing profile/name/PIN/totals, finish one game,
then reboot and confirm persisted totals. Prior hardware acceptance does not
cover this new storage path.

### 1. Profile login, virtual play and common controllers

User priority: deliver fully phone-only play and concurrent phone/physical control
before game-scoped statistics. The first implementation is Atlas `0.6.0-dev`;
see [Profile login and virtual play](PROFILE_LOGIN_AND_VIRTUAL_PLAY.md).

Implemented and automated checks pass: independent profile registration/login,
explicit join/leave, one participant per profile, simultaneous browser tokens,
phone-only lifecycle/gameplay, mixed participation, physically confirmed lobby
attachment, and statistics from the captured participant identity. The owner
reports compiling/flashing the current changes successfully and that everything
works (2026-09-20); this is general bench feedback, not a recorded pass of each
persistence/reboot acceptance case.

**On hold (owner, 2026-09-24): the two-button e-ink picker waits for a D-pad on
the Sigil.** The notes below stay as the design record.

**Physical profile selection and reusable Sigils.** The owner found that
Michael joining by phone prevents his last-used, unjoined Sigil from joining as
another person through its buttons. Saved bindings still supply physical join
identity. Preserve duplicate-profile protection, separate last-used preference
from live assignment, and add a two-button e-ink picker through Atlas's shared
application/Intent boundary. See [Physical Profile Selection](PHYSICAL_PROFILE_SELECTION.md)
for the concrete flow, compatibility slices and focused acceptance cases.
Owner decision (2026-09-21): independent profile settings for allowing physical
use without a PIN and hiding stats without authentication. Hidden stats still
accumulate. Same-profile browser login enables full display on an attached Sigil;
physical selection never blocks browser use. Startup offers last-profile or
selector behavior as a per-Sigil preference in an Atlas-owned Device Settings
area. Sigils do not persist user settings; Atlas supplies their runtime
configuration. The first local slice implements the two profile choices,
Atlas-side physical-join authorization and session-based stats-visibility policy,
with native/storage/browser checks and a firmware build. Existing naming is now
under Device Settings. Missing-policy defaults preserve physical use and hide
stats; the last active same-profile session controls authenticated visibility.
No hardware acceptance yet. Startup behavior and the picker still need coordinated
Atlas/Sigil changes, live-assignment separation and e-ink verification; they are
not implemented. Device-settings editing permissions beyond existing physical
Atlas confirmation for naming remain to be defined.

Follow-through remains: durable session/recovery policy, richer role/capability
administration, temporary browser guest profiles, broader controller handoff,
and any shared contract/version negotiation needed for Android. Current physical
attachment targets an unjoined primary seat in the lobby. Existing shared seats
retain profile companion control.

### 2. Accessibility foundation

- Treat WCAG 2.2 Level AA as the design baseline for web/app surfaces without claiming conformance until tested.
- Define reusable accessible UI patterns before the portal and Android surfaces diverge.
- Ensure essential states are not represented by color alone; pair color with text, icons, pattern/cadence, or another practical cue.
- Add high-contrast and monochrome-safe presentation options. The web portal has a
  High contrast theme (automatic when the device asks for more contrast) and honours
  `forced-colors`; Android switches to high-contrast colours when the system
  contrast is raised; Sigil lights have a Monochrome-safe style (all 2026-09-24).
  A monochrome-safe portal theme separate from High contrast remains open.
- Player-level accessibility preferences exist for the Sigil (sound, light style,
  hold times) and follow the profile; browser presentation stays per-browser.
  Further preferences (LED intensity, volume, e-ink text scale) remain open.
- Provide keyboard and assistive-technology semantics for essential web controls.
- Reduced motion: portal (OS setting or per-browser switch) and Sigil lights
  (Reduced motion style) are implemented; avoid rapid/seizure-risk flashing.
- Long-press and win-hold times are adjustable per player (Sigil 0.5.4). Atlas's
  pairing window is admin-adjustable (15/30/60 s); the 15-second life-approval
  window is not adjustable yet. LED intensity and buzzer volume are on hold
  (hardware).
- Preserve an authorized assistive-companion path for players when a table policy otherwise requires physical Sigils.
- Add accessibility checks to feature verification and future hardware review.

### 3. Game profiles and statistics separation

Owner direction (2026-09-24): long-term statistics are the goal for this lane.
Draws now reach the v1 statistics (last result `Draw`); a draw counter needs the
versioned statistics migration below.

- Introduce a stable `gameProfileId` so statistics are partitioned by game/format instead of one global lifetime bucket.
- Keep player identity independent from physical or virtual controllers.
- Preserve the current v1 aggregate statistics during migration.
- Preserve pre-game-profile totals as legacy/unclassified; do not infer their
  historical game type or manufacture historical match records.
- Separate universal facts from game-specific statistics.
- Treat raw session facts as canonical inputs and derived values such as win percentage as computed data.

Privacy direction:

- Public by default: basic participation facts such as games played.
- Private by default: derived performance statistics such as win rate, average turn time, and similar comparative metrics.
- Allow explicit opt-in sharing of private/derived statistics.
- Enforce visibility on Atlas before serialization, not only in the browser UI.
- The profile owner's hide-without-authentication choice also covers physical
  displays; hiding stats never disables recording. See the physical-selection
  policy for same-profile browser authentication and Sigil display access.

### 4. Extend local storage repositories

- Keep small critical configuration in NVS/preferences.
- Use the implemented blob/repository boundary before adding structured history.
- Define explicit portable encodings for new formats and test restart-safe
  migrations before replacing the deployed v1 statistics representation.
- Store player profiles, game profiles, session history, and exports independently from game-engine logic.
- Design for optional expanded local storage later without changing game semantics.
- Planned: once SD storage exists, serve optional portal theme packs from it as
  extra token sets; built-in themes stay in flash (see
  [Web Portal Design System](WEB_PORTAL_DESIGN.md)).
- Version stored schemas and define migrations before changing persistent formats.

### 5. Session history

- Add bounded local game/session records after the storage boundary exists.
- Session records should contain enough raw facts to rebuild aggregates where practical.
- Define retention limits so Atlas storage cannot grow without bound.
- Keep completed history separate from the smaller active-match recovery record;
  Prototype 1.0 recovery must not require full historical-session persistence.

### 6. Android app

- Build against the same versioned Intent, state, profile, and statistics contracts used by the browser.
- Do not introduce Android-specific game semantics.
- Respect platform accessibility services/settings where practical and use the shared accessibility requirements.

### 7. OTA and hardware hardening

- Real pairing with persistent associations, forgetting (Sigil hold and admin
  portal) and an adjustable Atlas window are implemented (see
  [Manual Pairing](MANUAL_PAIRING.md)); authenticated device trust remains.
- Re-test Atlas OTA application and reboot behavior on physical hardware.
- Define validation and rollback/recovery behavior.
- Choose the production Sigil transport and OTA strategy.
- Freeze hardware revisions only after GPIO, power, display, pairing, transport, tactile-control, and accessibility decisions are verified.

## Pending physical verification

- Pairing-window behavior and pairing LED mode.
- Current Atlas/Sigil hardware pin mappings.
- E-ink orientation, refresh behavior, legibility, and power measurements.
- Physical button distinguishability and long-press usability.
- LED states remain understandable without color alone.
- Atlas OTA after ESP32 migration.
- Production wireless transport decision.
- Prototype 1.0 interrupted-match recovery after abrupt Atlas power loss. The
  load/save wiring and diagnostic logging are in now (see item 3 above); this
  entry stays open until an actual power-loss-and-reboot bench test confirms
  it on hardware, including ending a restored match with the master-button hold.
- Master-button draw hold: 5 s during a match, status LED blinks fast from 1 s,
  a shorter press still passes, statistics record Draw once.
- Forgetting pairings: 10 s Sigil Pair hold; admin Forget one/all with `Unpair`
  reaching an in-range Sigil; the 30/60 s Atlas pairing window.

- Serial-log browser download (`GET /api/diagnostics/log`, Developer page
  button). *Needs verification* on hardware: host scenarios cover capture,
  redaction, overflow and the permission gate, but the ESP32 build, the
  cross-task spinlock and the 16 KB DRAM cost have not been confirmed on a
  flashed Atlas yet.
- *Planned* follow-up: capture framework `log_e`/ESP-IDF output as well (for
  example via a vprintf hook). Today only the Atlas `serialLog` stream is kept.
  Persisting logs across reboots needs storage that Atlas does not have yet.

## Working rules

1. Do not create a new long-lived branch for planning/documentation alone.
2. Keep near-term unimplemented work in this document.
3. Use short-lived branches only when code changes need isolation, review, or experimental protection.
4. Use `master` as the accepted baseline; the former `atlas-esp32-port` merge is complete.
5. Integrate subsequent changes only when affected builds/tests pass, required hardware checks pass, and architecture references match implemented behavior.
6. Important decisions should be reflected in repo documentation, not preserved only in chat.
7. Before any structural change, review the engineering Git documentation first, including the architectural invariants, this staging document, and every reference document materially affected by the change. Resolve documentation conflicts before changing structure.
8. Before treating a user-facing feature as complete, review its accessibility impact against `ACCESSIBILITY.md`.

Last updated: 2026-09-24
