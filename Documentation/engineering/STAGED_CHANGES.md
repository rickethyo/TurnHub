# TurnHub Staged Changes

Current pairing update (2026-09-22): physical buttons are owner-verified and manual
15-second pairing with persistent MAC associations is now implemented. The boot
pairing fallback and visual mock are superseded. Radio bench acceptance and
forget-device management remain pending. See [Manual Pairing](MANUAL_PAIRING.md).

Owner decisions (2026-09-25, end of day):

- **Retire "host" and "admin unlock".** The host (the first-joined controller,
  which alone may start, rematch, reset and pick the starter) and the 3 s
  touchscreen Admin unlock window (physical presence for first-Admin setup,
  network settings, device names and OTA) feel clunky and dated. They go away,
  but *some* access gate is still needed. **Accepted direction (owner,
  2026-09-25), *Planned*:**
  - **Table actions:** Start, Rematch and choosing the starter are open to any
    seated player. Start keeps its countdown, which any seated player can
    cancel, so no one player can force it. No host.
  - **Admin actions:** an Admin signs in on a phone. The Atlas screen then
    shows a short one-time code or QR code, which the Admin enters or scans on
    that phone to prove they are at the table. This replaces the 3 s hold.
  - **New or factory-reset Atlas:** the screen shows a setup QR code carrying
    a one-time token, and the first phone to use it becomes the Admin.
    Without that, a stranger on the Wi-Fi could claim an unconfigured Atlas.

  All three go through the Intent/validator boundary. **Implemented
  2026-09-25 (host-tested, *Needs verification* on hardware):**
  - **Host removed:** Sigil menus and gestures, the portal, the next-game
    settings and Android (its `host` flag now means "seated") all follow the
    any-seated-player rule.
  - **Presence code replaces the hold** (`front_panel.cpp`): an Admin taps
    **Verify at the table** in the portal. The Atlas screen shows six digits
    and a QR code for 90 s, and entering them on that phone verifies the Admin
    for 10 minutes. Five wrong codes cancel the code, and **Cancel** on the
    screen removes one. A code works only for the account that asked. Protected
    requests answer `403 presenceRequired`, and the portal then runs the flow
    and retries.
  - **New Atlas:** before any Admin exists, any signed-in account may ask for
    a code (the setup banner), so the setup QR is the same flow.
  - **Still open:** whether the Atlas touchscreen itself (which has no seat)
    may Start or Rematch.
- **SD cards are multi-Atlas.** A card records its owner Atlas's ID. A card
  from another Atlas is offered in transient "slots" with two choices:
  - **Game night:** its profiles, statistics and settings are available
    temporarily on this Atlas, and the card is not taken over.
  - **Merge in:** its data becomes this Atlas's master data, and the card now
    belongs to this Atlas.

  **Design open:** conflicts, meaning the same profile on both, whose PIN
  wins, and which stats are merged or kept apart. Also where game-night
  results are written.
- **Cards pulled mid-session must be re-recognized** when reinserted (hot-plug
  remount), not left out until a restart.
- **No statistics rollback option.** Detail moved to the card is not copied
  back to NVS on request.
- **Sigil OTA over Wi-Fi first (owner accepted the recommendation, 2026-09-25;
  *Planned*).**
  - **How it works:** Atlas tells a Sigil over ESP-NOW to update, sending the
    Wi-Fi credentials over the paired link. The Sigil joins Atlas's AP and
    downloads the image over HTTP with the ESP32's standard update library.
    Expect about 20-40 s per Sigil, against 1-3 min for about 3,300 ESP-NOW
    packets.
  - **Recovery:** each Sigil keeps two app slots. A new image is kept only
    once it has reconnected to Atlas; otherwise the bootloader returns to the
    previous one.
  - **Fallback:** ESP-NOW transfer, if Wi-Fi joining proves unreliable.
- **Factory reset: implemented (2026-09-25, host-tested, *Needs verification*
  on hardware).** Device Settings has Factory reset per Sigil and for Atlas
  (NVS erase and restart; the microSD card is kept). See
  [Manual Pairing](MANUAL_PAIRING.md#factory-reset-2026-09-25). The user
  manual V0.3 covers it.


Owner decisions and follow-through (2026-09-24, later the same day):

- **End a match as a draw:** implemented. Holding End match on the Atlas
  touchscreen for 5 s (originally the master button, removed later that day)
  during a running or paused match (including a recovered one) sends `EndMatch`,
  which ends it with no winner and records a Draw for every player. This is the
  "Discard" path for a recovered match that item 3 below lacked.
- **Forget pairings:** implemented. A 10 s Sigil Pair hold erases the Sigil's
  pairing; admins forget one or all Sigils in Device Settings, and Atlas sends
  `Unpair`. See [Manual Pairing](MANUAL_PAIRING.md#forgetting-a-pairing-2026-09-24).
- **Pairing window:** Atlas's window is admin-adjustable (15/30/60 s).
- **On hold:** buzzer volume (current hardware cannot vary it). Unblocked
  (2026-09-25): the physical profile picker (both Sigils now have five-way
  input: the E-ink joystick and the OLED d-pad, with Sigil menus) and LED
  brightness (the NeoPixel Jewel ring can dim; Atlas already sends `LedState`).
- **Not planned on e-ink:** the turn timer on the Sigil screen; it will be tried
  on an LCD Sigil model.
- **Long-term goal:** game-scoped statistics and session history (sections 3-5).

All of the above: host scenarios, the portal browser smoke, and PlatformIO
builds of Atlas, `sigil` and `sigil-wokwi` passed at the time (Sigil firmware 0.5.5-dev; now 0.7.0-dev). The
Android "Draw" label has a unit test that was not run. Nothing was flashed;
hardware acceptance is pending.

Turn timer and cue layers (2026-09-24): the Atlas-owned turn timer, LED cue
profile, audio cue profile and Android player controls are implemented locally with
host/Android tests and an Atlas build; hardware acceptance is pending. See
[Turn timer and cues](TURN_TIMER_AND_CUES.md). Staged follow-ups:

- Timer on the Sigil screen: not on e-ink (owner decision); revisit on an LCD Sigil.
- Partly done (2026-09-25): Sigils render the light themselves from Atlas's
  `LedState`, and the local Pairing blink follows Reduced motion. Still to
  check: Sigil-local Disconnected/Error looks honor the player's light style
  (Standard, Reduced motion, Monochrome-safe).

Sigil accessibility preferences (2026-09-24): the owner chose per-player settings
that follow the profile. Sigil sound, light style (Standard, Reduced motion,
Monochrome-safe) and adjustable Action hold times are implemented with host,
browser and Android tests and firmware builds (Atlas, Sigil 0.5.4); `ActionRequired`
now sounds for win confirmations and life-change recipients. Hardware acceptance
is pending: see the bench list in [Accessibility](ACCESSIBILITY.md#implemented-accessibility-settings).
Remaining accessibility items are listed there under "Not yet implemented".

Atlas display board (2026-09-24): Atlas now targets the LCDwiki E32R28T 2.8"
ESP32-32E display module (see [Hardware reference](HARDWARE_REFERENCE.md#generation-2-development-atlas)).
Implemented and built: the new pin map, the splash screen and the `min_spiffs`
partition table. The Pair button, the status/Pair LEDs and (owner decision,
2026-09-24) the master button are gone; the firmware reads no buttons. The
touchscreen is Atlas's only physical input: Pair a Sigil, Start/Cancel
start/Rematch/Reset between games, Pause/Resume, and a Table screen with a 2 s
**Master pass** hold for a stuck turn and a 5 s hold to end the match as a
draw. Since 2026-09-25 it has no Pass button (players pass from their seats),
and presence codes replaced the old **Unlock admin** hold (see
[Hardware reference](HARDWARE_REFERENCE.md#atlas-touchscreen)).
The on-board speaker now plays table-wide cues at an Admin-chosen volume (see
[Atlas speaker](HARDWARE_REFERENCE.md#atlas-speaker)). Staged follow-ups:

- **Bench acceptance:** screen orientation, the 10 s lobby recalibration hold,
  each touch button against a real table, the
  Unlock admin window (first Admin, a network save, an OTA upload, Return
  table to lobby from the portal), and the speaker at each volume. On-device
  touch calibration and touch accuracy are *Verified* by the owner after the
  2026-09-25 touch read fix (see [Hardware reference](HARDWARE_REFERENCE.md#atlas-touchscreen)).
- **microSD storage:** step 1 is done (2026-09-24): Atlas mounts the card at
  boot, runs a write/read-back self-test and reports it in Developer
  diagnostics; `SdBlobStore` provides checksummed records with backup fallback
  when the filesystem remains intact (see
  [Identity and storage](IDENTITY_AND_STORAGE.md#optional-microsd-storage)).
  *Verified* by the owner on hardware: the card mounts and registers. Still to
  check: booting without a card.
  Step 2 (2026-09-25, Codex, `codex/sd-diagnostics`): optional rotating
  redacted diagnostics and boot markers are implemented as an experiment;
  self-test failure now gates consumers. See [SD diagnostics](SD_DIAGNOSTICS.md)
  for the remaining physical checks.
  **Owner direction (2026-09-25):** offload as much as possible to the card.
  An Atlas without a card runs in a "limp" mode: it loses luxury features but
  stays fully functional for play. So core table function (pairing, seats,
  the game engine, settings, recovery, and whatever login needs) must never
  depend on the card. Bulk and history records (statistics, session and game
  history, logs) move to it, and are shown as unavailable when it's missing.
  **The split (owner, later on 2026-09-25; flash and NVS are running short):**
  NVS keeps only a basic profile (ID, name, PIN hash; no avatar, no extra
  themes or cosmetic options) and a few basic statistics: games played, games
  won, the game type, perhaps a handful of other simple counters. Literally
  everything else goes to the card.
  **Step 3 done (2026-09-25, host-tested, *Needs verification* on hardware):**
  statistics are split into an NVS core record (games played and won, last
  result, last game type) and the detailed record on the card, with a
  checked, non-destructive migration at boot (see
  [Identity and storage](IDENTITY_AND_STORAGE.md#persistence-ownership)).
  The portal's stats page says when detail needs the card. Deliberately kept
  in NVS, for the owner to confirm:
  - account permissions and profile policy, because they gate login;
  - per-player accessibility preferences, because accessible play is not a
    luxury;
  - moderation counts, because a Game Master action records them before it
    acts and would be refused without a card.

  **Measured 2026-09-25:** NVS was at 130 of 630 entries with one profile
  (about 14 entries each), so NVS alone would fill somewhere past 30 profiles.
  The tighter space is program flash (1.33 MB of the 1.97 MB app slot). The
  largest single item is the portal page, 103 KB of uncompressed HTML, plus
  19 KB of CSS and 10 KB for the stats page. Storing them gzipped, as the QR
  script already is, would free roughly 100 KB with or without a card.
  Proposed, not started.
  Decided 2026-09-25 (see the owner decisions at the top of this file): no
  statistics rollback. A card pulled mid-session must be re-recognized when
  reinserted; today it still needs a restart, so hot-plug remount is to
  build. Cards are multi-Atlas, with owner ID plus game-night or merge-in
  slots; to design.
- Done 2026-09-25: player names, life and the turn clock on the status
  screen, Info and QR code screens, the NO SD CARD warning (bench check of
  the new layout pending). Still open: more touch actions (Start, Rematch,
  starter selection) from the touchscreen, which has no seat of its own. The
  access gate is built (top of this file); this is the remaining question. Also open: the
  on-board RGB LED as a cue output; a battery gauge.
- User manual: **V0.3 written 2026-09-25** (`Documentation/User Manual/TurnHub
  Manual V0.3.docx`). It covers the Atlas touchscreen (Pair, QR codes, Info,
  the End hold, recalibration), the menu-driven E-ink and OLED Sigils, no table
  host, Verify at the table, factory reset, statistics and the microSD card,
  and restarting without a shut-down command. Keep it in step with later
  changes.

This is the durable staging document for agreed work that has not yet been implemented or fully verified.

Use this file instead of chat history for near-term changes. Keep it concise. Once an item is implemented and verified, move any lasting architectural facts into the appropriate reference document and remove it from here.

## Current baseline

### Next implementation priority: Sigil OTA

Owner direction (2026-09-25): move Sigil OTA ahead of session history and SD
theme packs. Repeated USB flashing and COM-port tracking across devices is the
immediate workflow problem. Continue the already-in-progress profile/life work
without mixing it into the updater change.

Target workflow: upload each firmware variant to Atlas once, select paired
Sigils by name, update one device first and then a queued group, with visible
transfer/install/reboot results and reported running versions. Atlas stages
validated images on SD and retains the current known-good package plus a small
history of compatible previous releases for an explicit reinstall action.
See [package storage requirements](SD_DIAGNOSTICS.md#sigil-update-packages-planned).

Implementation order:
1. Define variant/version/compatibility metadata and the Sigil update transport,
   flash partition requirements and boot validation/recovery behavior.
2. Add authenticated Admin upload/catalog handling with the existing physical
   unlock policy, verified staging and bounded package retention on SD. A hash
   detects corruption; update authorization/authenticity is a separate requirement.
3. Prove an end-to-end update on one Sigil, then add per-device queuing, progress,
   timeout/retry reporting and selection of a retained compatible image.

Acceptance: update and reinstall a retained compatible release on both Sigil
variants without selecting COM ports; reject wrong-variant/corrupt images;
interrupt transfer/power and verify a recoverable device. Do not claim automatic
rollback until failed-boot recovery is implemented and tested. Full Sigil OTA
remains planned; the SD diagnostics change does not implement firmware delivery.

### Baseline reference

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

   Update (2026-09-24): the owner chose a 5-second Atlas hold (first the master
   button, now End match on the touchscreen, since the master button is gone) that
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
   indistinguishable from that once `hubState` is `Paused`. The End match
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

**Unblocked (2026-09-25):** this waited for a D-pad on the Sigil (owner,
2026-09-24). Both Sigils now have five-way input (E-ink joystick, OLED d-pad)
and a Sigil menu system, so the picker can be designed on those. The notes
below stay as the design record.

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
not implemented. Device settings (naming, forgetting, factory reset) need an
Admin, and naming and factory reset also need that Admin verified at the table
(presence code, 2026-09-25).

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
  High contrast theme (automatic when the device asks for more contrast) and honors
  `forced-colors`; Android switches to high-contrast colors when the system
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
- Sigil OTA is the next implementation priority above. Its transport is
  Wi-Fi download with two-slot rollback (owner accepted, 2026-09-25; see the
  owner decisions at the top); ESP-NOW transfer stays the fallback.
- Freeze hardware revisions only after GPIO, power, display, pairing, transport, tactile-control, and accessibility decisions are verified.

## OLED Sigil

- **One player per OLED Sigil:** implemented and host-tested (2026-09-24),
  *Needs verification* on hardware (see the
  [hardware reference](HARDWARE_REFERENCE.md#experimental-oled-display-variant)).
  Feature gate: state owner Atlas (`Lobby`); Intent: the existing `Join`
  (slot 2) and `ArmStart`/`StartGame`; validator: `sigilSeatsOnePlayer()` in the
  seat-membership and start handlers; persistence: none (the capability is RAM,
  re-sent in every Hello); rendering: the portal device list; contract: new
  `CAPABILITY_DISPLAY_OLED` bit in `shared/include/protocol.h` (no packet layout
  change; reflash Atlas and the OLED Sigil, e-paper Sigils may stay); no new
  dependency; accessibility: the refusal is text in the log/portal, never only a
  sound or LED. Bench check: flash both, try the Action + PASS chord on the OLED
  Sigil (no Seat B), and confirm the portal badge.
- *Planned:* a Sigil-side message when Seat B is refused (today the second seat
  just does not appear), and the user manual once the OLED Sigil ships.
- *Needs verification:* OLED rotation 0 after the panel was remounted.

## E-ink Sigil: player-facing LED strip (possible, depends on the case)

*Planned, conditional* (owner, 2026-09-26). The enclosure concept is a 45° wedge
about 48 mm wide, 78 mm deep and 78 mm tall: portrait e-paper on the upper
slope, joystick below it, main board flat in the base, USB through the back
wall. In that shape the Jewel 7 status ring faces the **other players** from the
back wall, so the seated player can't see it. A short LED strip on the front lip
would give the **player** their own light. Build this only if the final case
keeps that split; if the case lets one light face both ways, drop it.

- **Hardware:** no Sigil board change. The strip hangs off the Jewel adapter's
  existing chain-out, so it's the same data line (GPIO26 through U2 and J5):
  Jewel pixels 0–6, then the strip. Candidate: 6–8 SK6812 RGBW pixels to match
  the Jewel's colour order. The Jewel adapter's chain-out connector and cable
  length depend on where the case puts the strip.
- **Feature gate:**
  1. *State owner:* Atlas, unchanged. It already decides each Sigil's
     `LedState`; the Sigil only renders it.
  2. *Intent:* none. Lights are output only and add no gameplay action.
  3. *Validator:* none new. Any user setting (see 5) uses the existing
     accessibility preference path.
  4. *Persistence:* the pixel count is a build or hardware setting on the Sigil,
     not NVS. A per-player front-strip brightness, if added, belongs in the
     existing per-player accessibility preferences (`optional_preferences`).
  5. *Rendering:* the Sigil maps one `LedState` to two groups. The rear ring
     keeps today's table-facing role (turn state, player colour). The front strip
     shows the player's own cues (your turn, timer warnings, pending life
     approval). The exact split is an owner decision.
  6. *Protocol/contract:* probably none. If both groups are derived from
     `LedState` on the Sigil, `shared/include/protocol.h` doesn't change. A
     separate front-strip state from Atlas would be a radio contract change and
     need both firmwares reflashed.
  7. *Third-party dependencies:* none new (Adafruit NeoPixel already drives the
     Jewel).
  8. *Accessibility:* no information may live only on the strip; the e-paper
     repeats everything it shows. Brightness should be adjustable because the
     strip is a few centimetres from the player's eyes. Blink patterns follow
     the existing reduced-motion style (`LedStyle::ReducedMotion`).
- **Power:** USB can't supply full RGBW on 7 + 8 pixels. The firmware brightness
  cap (48/255) has to hold for the whole chain, and the front strip can run
  dimmer than the ring. *Needs verification:* measure current with both groups
  at the cap.
- **Before building:** settle the case (strip position, pixel count, cable
  route), then the owner's choice of which cues go front and which go rear.

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
  it on hardware, including ending a restored match with the End match hold.
- End match draw hold on the touchscreen (on the Table screen since
  2026-09-25): 5 s during a match with an on-screen countdown, a shorter press
  only shows a hint, statistics record Draw once.
- Touchscreen table actions (2026-09-25, flashed, *Needs verification*): Start
  with two or more players, Cancel start, Rematch and Reset after a game, and
  the Table screen's 2 s Master pass hold passing a stuck turn at once with an
  `ATLAS|GAME|MASTER_PASS` line in the serial and SD logs. Also check the
  speaker levels, lowered about 10% the same day, at each volume setting.
- Preset avatars (2026-09-25, host-tested, *Needs verification*): pick one in
  the portal's Personalization card (needs the SD card); shown in the portal,
  the Android app, Atlas's player chips and the OLED game screen (not the
  e-ink yet). Custom avatars: scaffolding only (`AVATAR_CUSTOM` reserved,
  never public); upload and Admin approval to make one public are *Planned*.
  Feature gate: profile setting on the card (`v<profileId>`), no Intent,
  `/api/avatars` + `/api/seats` contract additions, icons drawn for TurnHub
  (no third-party art), decorative only (names always shown).
- Jewel color per profile (2026-09-25, host-tested, *Needs verification*):
  pick a color in the portal (needs the SD card); the Jewel shows it in the
  lobby and while waiting, action cues unchanged, shared Sigils split by seat.
- Turntest notes (2026-09-26, host-tested, *Needs verification*): softer
  warm white on the Jewel (was full RGB white); OLED one-line key help under
  the life total; a pending pass shows PASSING and a green ring countdown on
  every Sigil (green countdown for the passer, amber plus "P<n> PASSING"
  for everyone else, `PassPending = 38`) and "Passing in Ns" on Atlas, with a
  table-wide tick when it starts and when it is undone; a second Select on
  the OLED undoes it; the life heart drains or grows against the starting life
  (`StartingLife = 37`); US spellings in UI text, comments and docs (wire
  tokens such as `CANCELLED` and code identifiers unchanged).
- E-ink partial refresh with clean-ups (2026-09-25, *Needs verification*):
  tune with `epd max/idle` over serial and report fading.
- Sigil life (2026-09-25, host-tested, *Needs verification*): Left/Right
  change life on both Sigils (tap, hold, fives after 1.5 s), one send 2 s after
  the last press, the e-ink Jewel showing the running total; life requests
  from phones show on the target's Sigil with Right approve / Left deny. Known
  gap: while paused the request text is not on the status screen, only the
  Approve/Deny legend (e-ink).
- Leave lobby on menu Sigils (2026-09-25, host-tested, *Needs verification*):
  a joined e-ink or OLED Sigil (0.8.0) holds Down (or Left) to leave both
  seats; the next Join shows the picker fresh and Guest is really a guest.
- Lobby Clear on the Atlas touchscreen (2026-09-25, *Needs verification*): shown
  once anyone has joined; a 2 s hold (`LOBBY_CLEAR_HOLD_MS`, `ResetGame` from
  Atlas hardware) empties the lobby, a tap only explains. Check the six-button
  row with a harness connected is still easy to hit.
- Atlas screen flicker fix (2026-09-25, *Needs verification*): the turn clock,
  countdown bar and a held button's fill bar are drawn over the old pixels
  instead of cleared first, and a hold repaints only its own button. Check a
  running timer and each hold button (End match, Master pass) for blinking.
- E-ink Sigil profile picker (2026-09-25, host-tested, *Needs verification*;
  Atlas and Sigil 0.8.0 both need flashing): Join opens the picker; Guest,
  paging, confirm/back, a "phone sign-in" profile refused with its reason and
  accepted once its owner signs in on a phone, attaching to a phone-joined
  profile, idle close after a minute, and an older Sigil still joining as a
  guest. The OLED Sigil shows it as a list (Up/Down, Select/Right, Left). See [Physical profile selection](PHYSICAL_PROFILE_SELECTION.md#e-ink-sigil-picker-2026-09-25).
- Forgetting pairings: 10 s Sigil Pair hold; admin Forget one/all with `Unpair`
  reaching an in-range Sigil; the 30/60 s Atlas pairing window.

- Serial-log browser download (`GET /api/diagnostics/log`, Developer page
  button). *Needs verification* on hardware: host scenarios cover capture,
  redaction, overflow and the permission gate, but the ESP32 build, the
  cross-task spinlock and the 16 KB DRAM cost have not been confirmed on a
  flashed Atlas yet.
- *Planned* follow-up: capture framework `log_e`/ESP-IDF output as well (for
  example via a vprintf hook). Today only the Atlas `serialLog` stream is kept.
  Optional SD persistence is now experimental; see [SD diagnostics](SD_DIAGNOSTICS.md).

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
