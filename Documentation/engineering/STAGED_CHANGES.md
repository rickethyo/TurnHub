# TurnHub Staged Changes

Current pairing update (2026-09-22): physical buttons are owner-verified and manual
15-second pairing with persistent MAC associations is now implemented. The boot
pairing fallback and visual mock are superseded. Radio bench acceptance and
forget-device management remain pending. See [Manual Pairing](MANUAL_PAIRING.md).


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

   Deliberately not done: there is still no Resume/Discard *decision* UI.
   "Resume" needs no new code -- a restored match lands in the existing
   `Paused` state, so the current Pause/Resume controls (physical and
   browser) already continue it. "Discard" has nothing to hook into yet:
   `ResetGame` is intentionally only reachable from `GameOver` or an
   unstarted `Lobby` today, specifically so it can't be used to nuke an
   ordinary in-progress paused game, and a freshly-recovered match is
   indistinguishable from that once `hubState` is `Paused`. Building this
   safely needs a real decision (a new `HubState`, a boot-scoped "still
   exactly what was recovered" guard, and a place to trigger it physically
   for a table with no phone in reach) rather than a quick guard-clause
   change, so it was left as a follow-up rather than guessed at here. Until
   it exists, an unwanted recovered match can only be cleared by playing it
   out (Concede down to a winner) or by erasing NVS.
4. **Wire the auxiliary-button software path before final GPIO wiring.** `BTN_AUX`
   remains a hardware abstraction. Its requests must resolve to existing semantic
   actions/Intents instead of creating game rules tied to a button. Firmware may
   compile with the auxiliary input disabled until the physical switch is installed.
5. **Replace passive discovery with the real pairing state machine.** Prototype 1.0
   pairing uses a deliberate 15-second pairing window and Atlas-owned trust state.
   Until `BTN_PAIR` is physically wired, an unpaired Sigil may enter that same real
   pairing window automatically at boot. The temporary boot trigger must not become
   a separate pairing implementation. The physical Pair button may slip past the
   field-test handoff; the pairing architecture may not.
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

**Next: physical profile selection and reusable Sigils.** The owner found that
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
- Add high-contrast and monochrome-safe presentation options.
- Define player-level accessibility preferences separately from game profiles.
- Determine which accessibility settings follow a player versus remain device-local.
- Provide keyboard and assistive-technology semantics for essential web controls.
- Add reduced-motion behavior and avoid rapid/seizure-risk flashing.
- Make long-press and other timing-sensitive physical interactions adjustable where practical without changing game semantics.
- Preserve an authorized assistive-companion path for players when a table policy otherwise requires physical Sigils.
- Add accessibility checks to feature verification and future hardware review.

### 3. Game profiles and statistics separation

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

- Implement real pairing/trusted-device persistence separately from the current
  five-second LED mock; define re-pair, forget and normal reconnect behavior.
- Prototype 1.0 target: 15-second real pairing window; until the Sigil Pair button
  is wired, unpaired Sigils may use boot as the temporary trigger for that same flow.
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
  it on hardware, and until the Resume/Discard decision UI exists.

## Working rules

1. Do not create a new long-lived branch for planning/documentation alone.
2. Keep near-term unimplemented work in this document.
3. Use short-lived branches only when code changes need isolation, review, or experimental protection.
4. Use `master` as the accepted baseline; the former `atlas-esp32-port` merge is complete.
5. Integrate subsequent changes only when affected builds/tests pass, required hardware checks pass, and architecture references match implemented behavior.
6. Important decisions should be reflected in repo documentation, not preserved only in chat.
7. Before any structural change, review the engineering Git documentation first, including the architectural invariants, this staging document, and every reference document materially affected by the change. Resolve documentation conflicts before changing structure.
8. Before treating a user-facing feature as complete, review its accessibility impact against `ACCESSIBILITY.md`.

Last updated: 2026-09-23
