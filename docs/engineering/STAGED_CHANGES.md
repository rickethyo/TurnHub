# TurnHub Staged Changes

This is the durable staging document for agreed work that has not yet been implemented or fully verified.

Use this file instead of chat history for near-term changes. Keep it concise. Once an item is implemented and verified, move any lasting architectural facts into the appropriate reference document and remove it from here.

## Current baseline

Active baseline: `master` (Atlas ESP32 migration merged in `6844f94`).

The owner confirmed the existing repository build and installed hardware on
2026-09-20. New changes still require their own applicable verification.

Current product rule: Atlas is authoritative for canonical game/table state. Physical Sigils, virtual Sigils, browsers, simulators, and future apps request semantic actions through the shared Intent boundary and render Atlas state.

Accessibility is a hard product requirement. User-facing features must follow `ACCESSIBILITY.md`; essential information or actions must not depend on a single sensory cue or input method when a practical alternative exists.

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
Standalone physical authorization (possession versus PIN) remains undecided.
This needs coordinated Atlas/Sigil changes; the picker is not implemented.

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

### 6. Android app

- Build against the same versioned Intent, state, profile, and statistics contracts used by the browser.
- Do not introduce Android-specific game semantics.
- Respect platform accessibility services/settings where practical and use the shared accessibility requirements.

### 7. OTA and hardware hardening

- Implement real pairing/trusted-device persistence separately from the current
  five-second LED mock; define re-pair, forget and normal reconnect behavior.
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

## Working rules

1. Do not create a new long-lived branch for planning/documentation alone.
2. Keep near-term unimplemented work in this document.
3. Use short-lived branches only when code changes need isolation, review, or experimental protection.
4. Use `master` as the accepted baseline; the former `atlas-esp32-port` merge is complete.
5. Integrate subsequent changes only when affected builds/tests pass, required hardware checks pass, and architecture references match implemented behavior.
6. Important decisions should be reflected in repo documentation, not preserved only in chat.
7. Before any structural change, review the engineering Git documentation first, including the architectural invariants, this staging document, and every reference document materially affected by the change. Resolve documentation conflicts before changing structure.
8. Before treating a user-facing feature as complete, review its accessibility impact against `ACCESSIBILITY.md`.

Last updated: 2026-09-20
