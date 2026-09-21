# TurnHub Staged Changes

This is the durable staging document for agreed work that has not yet been implemented or fully verified.

Use this file instead of chat history for near-term changes. Keep it concise. Once an item is implemented and verified, move any lasting architectural facts into the appropriate reference document and remove it from here.

## Current baseline

Active development branch: `atlas-esp32-port`

Current product rule: Atlas is authoritative for canonical game/table state. Physical Sigils, virtual Sigils, browsers, simulators, and future apps request semantic actions through the shared Intent boundary and render Atlas state.

Accessibility is a hard product requirement. User-facing features must follow `ACCESSIBILITY.md`; essential information or actions must not depend on a single sensory cue or input method when a practical alternative exists.

## Staged implementation order

### 1. Game profiles and statistics separation

- Introduce a stable `gameProfileId` so statistics are partitioned by game/format instead of one global lifetime bucket.
- Keep player identity independent from physical or virtual controllers.
- Preserve the current v1 aggregate statistics during migration.
- Separate universal facts from game-specific statistics.
- Treat raw session facts as canonical inputs and derived values such as win percentage as computed data.

Privacy direction:

- Public by default: basic participation facts such as games played.
- Private by default: derived performance statistics such as win rate, average turn time, and similar comparative metrics.
- Allow explicit opt-in sharing of private/derived statistics.
- Enforce visibility on Atlas before serialization, not only in the browser UI.

### 2. Common controller layer

- Make physical and virtual Sigils use the same semantic action layer wherever practical.
- Define a controller interface usable by browser, simulator, Android, and physical Sigil adapters.
- Controllers may cache presentation state but never own canonical game state.

### 3. Accessibility foundation

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

### 4. Local storage boundary

- Keep small critical configuration in NVS/preferences.
- Introduce a storage-manager boundary before adding large structured history.
- Store player profiles, game profiles, session history, and exports independently from game-engine logic.
- Design for optional expanded local storage later without changing game semantics.
- Version stored schemas and define migrations before changing persistent formats.

### 5. Session history

- Add bounded local game/session records after the storage boundary exists.
- Session records should contain enough raw facts to rebuild aggregates where practical.
- Define retention limits so Atlas storage cannot grow without bound.

### 6. Virtual Sigils and web portal

- Finish fully virtual Sigils on top of the same Intent/state contracts as physical Sigils.
- Role/profile authorization determines what each browser session can see or control.
- Web presentation should not duplicate game rules.
- Apply the accessibility foundation to virtual controls rather than retrofitting it later.

### 7. Android app

- Build against the same versioned Intent, state, profile, and statistics contracts used by the browser.
- Do not introduce Android-specific game semantics.
- Respect platform accessibility services/settings where practical and use the shared accessibility requirements.

### 8. OTA and hardware hardening

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
4. Keep `atlas-esp32-port` as the active product-development branch until its merge gate is satisfied.
5. Do not merge to `master` until the ESP32 line is build-clean, host tests pass, required hardware regression checks pass, and the architecture references match implemented behavior.
6. Important decisions should be reflected in repo documentation, not preserved only in chat.
7. Before any structural change, review the engineering Git documentation first, including the architectural invariants, this staging document, and every reference document materially affected by the change. Resolve documentation conflicts before changing structure.
8. Before treating a user-facing feature as complete, review its accessibility impact against `ACCESSIBILITY.md`.

Last updated: 2026-09-20
