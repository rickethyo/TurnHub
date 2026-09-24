# TurnHub

TurnHub is a local-first tabletop game-management platform built around an authoritative **Atlas** table controller and player-facing **Sigils**. It began as a physical turn timer and is evolving into a mixed hardware/software platform where physical Sigils, browser controllers, and future native apps all request the same semantic actions from Atlas.

**Current development baseline:** ESP32 Atlas firmware **0.6.0-dev** on `master`.
The older Python/Raspberry Pi implementation under `Controller/` remains a historical and behavioral reference. It is not the current runtime and some of its features have not yet been reimplemented on ESP32.

See [Atlas/README.md](Atlas/README.md) for the current firmware/runtime details, [Documentation/engineering](Documentation/engineering/README.md) for the engineering record, staged work, architectural invariants, and verification backlog, and [Product Principles](Documentation/PRODUCT_PRINCIPLES.md) for the durable product values that guide design and commercialization decisions.

## Core architectural rule

**Atlas owns canonical game and table state.**

Physical Sigils, browser/Virtual Sigils, Atlas controls, simulators, and future applications submit requests through the shared Intent boundary and render Atlas state. They do not independently advance turns, resolve victories, own player identity, or maintain competing game engines.

A useful high-level model is:

```text
Profile (optional persistent person)
  -> Participant (one person in the current table/game)
    -> Controller assignment(s)
       -> Physical Sigil
       -> Browser / Virtual Sigil
       -> future native app/controller
```

Changing controllers should not replace the participant or move that person's statistics/game identity.

## Current ESP32 capabilities

Atlas `0.6.0-dev` currently provides:

- ESP32-hosted authoritative lobby and game engine.
- Local `TurnHub-Atlas` Wi-Fi access point and browser portal.
- ESP-NOW communication with development Sigils on the current prototype transport.
- Physical and phone-only participation, including mixed physical/browser tables.
- Local profile creation/login with PIN-based browser authentication.
- Local account permissions for Admin, Game Master and Developer, with initial Admin setup and private moderation counters (implemented locally; hardware playtest pending). See [Accounts and moderation](Documentation/engineering/ACCOUNTS_AND_MODERATION.md).
- Multiple browser sessions controlling the same participant.
- Explicit browser-assisted attachment of an unused physical Sigil to the authenticated profile.
- Shared physical A/B seats retained for compatibility.
- Starter selection, start/cancel countdown, turn passing, three-second cancellable pass grace, pause/resume, concession/elimination, confirmed victory, rematch, and reset.
- Game profile presets and custom starting life, with browser controls for each player's own life and public table totals (implemented locally; hardware playtest pending). See [Game profiles and life](Documentation/engineering/GAME_PROFILES_AND_LIFE.md).
- Atlas-authoritative profile statistics stored locally.
- Persistent profile identity, names/PIN-related profile data, physical-seat bindings, and deployed statistics through Atlas local storage.
- Browser and physical gameplay paths converging on the Atlas Intent layer.
- Local web OTA with existing physical/state gating.

Current implementation limits and acceptance details are documented in [Profile login and virtual play](Documentation/engineering/PROFILE_LOGIN_AND_VIRTUAL_PLAY.md) and [Atlas intent verification](Documentation/engineering/ATLAS_INTENT_VERIFICATION.md).

## Current persistence behavior

Atlas is **partially persistent** today.

Survives reboot/power loss:

- Durable player profiles and names.
- PIN-related profile data.
- Existing physical-seat/profile bindings or preferences.
- Deployed profile statistics.
- Selected game profile and starting-life preference.
- Other explicitly stored Atlas configuration such as the current AP settings.

Does **not** currently survive reboot:

- Browser sessions.
- Current table participation.
- Live controller assignments.
- Active game/turn state.
- Current life totals.

A current Atlas reboot therefore returns to a fresh table while preserving durable profile/statistics data. Prototype 1.0 work is staged to add a compact, versioned interrupted-match recovery record with Resume/Discard behavior and paused recovery so power-off time is never charged to a player. See [Software Architecture](Documentation/engineering/SOFTWARE_ARCHITECTURE.md) and [Staged Changes](Documentation/engineering/STAGED_CHANGES.md).

## Physical Sigils

The current development Sigil firmware uses an ESP32, two physical gameplay buttons, discrete status LEDs, buzzer output, a monochrome e-ink display, and ESP-NOW for the current experimental radio transport.

The current verified development wiring is maintained in [Hardware Reference](Documentation/engineering/HARDWARE_REFERENCE.md). Planned Prototype 1.0 work includes:

- Physical profile selection on reusable Sigils.
- A software path for a dedicated auxiliary Action/Win control before final GPIO wiring.
- Bench validation of the implemented Atlas-owned pairing records.
- A 30-second deliberate pairing window triggered by physical Pair buttons.
- No automatic pairing on boot; saved devices reconnect to their paired Atlas.
- Additional field-test Sigils and protective prototype enclosures.

The final production transport, power system, PCB revisions, battery design, and mechanical design are not frozen.

## Profile login and virtual play

A player can create a local profile or sign in from the Atlas portal without owning a physical Sigil. Logging in is separate from joining the current table.

The first participant is host. Phone-only players can complete the supported game lifecycle through Atlas's existing Intent handlers. Multiple authenticated browser sessions may control the same participant, and a physical Sigil may be attached to that same participant in the lobby.

A known remaining gap is standalone physical profile selection. A Sigil currently remembers its saved profile binding, which can conflict when that profile already joined by phone. Duplicate-participant protection is correct; the missing feature is choosing another profile on the reusable physical Sigil. The planned flow is documented in [Physical Profile Selection](Documentation/engineering/PHYSICAL_PROFILE_SELECTION.md).

## Pairing status

Manual prototype pairing is implemented, with hardware acceptance pending.
Press Atlas Pair in the lobby and Sigil Pair within the 30-second window.
Both devices persist their association; booting an unpaired Sigil no longer
broadcasts discovery or creates an association. Saved devices reconnect directly.
This is MAC-based association on the existing unencrypted experimental transport,
not production cryptographic authentication. See [Manual Pairing](Documentation/engineering/MANUAL_PAIRING.md).

## Local-first direction

TurnHub's core play path is intended to remain usable without a cloud service, subscription, or internet connection. Atlas serves the table locally, owns the game state locally, and stores supported persistent information locally.

The broader ownership, repairability, privacy, accessibility, longevity, and anti-lock-in direction is recorded in [Product Principles](Documentation/PRODUCT_PRINCIPLES.md).

First-run provisioning, optional home-network integration, fallback access, and recovery-mode direction are documented in [Atlas/OOBE.md](Atlas/OOBE.md). Home/LAN mode is not yet the current default and must account for ESP32 Wi-Fi/ESP-NOW channel constraints before being treated as production-ready.

## Prototype 1.0 field-test goal

Prototype 1.0 is the first system intended to leave the development bench for independent real-world testing. It is not a production release.

For that milestone, reliability and independent setup matter more than cosmetic finish. Rough soldered hardware and simple 3D-printed enclosures are acceptable. Internal battery integration is not required; known-good USB power banks are acceptable if setup and operation are clear.

The current critical path is maintained in [Staged Changes](Documentation/engineering/STAGED_CHANGES.md). It prioritizes reusable physical profile selection, web-portal feature freeze, interrupted-match recovery groundwork, auxiliary-button semantics, real pairing, three-Sigil field hardware, out-of-box setup, and hardening.

## Engineering documentation

TurnHub uses repository documentation as the durable engineering record. Before structural changes, review:

- [Product Principles](Documentation/PRODUCT_PRINCIPLES.md)
- [Architectural Invariants](Documentation/engineering/ARCHITECTURAL_INVARIANTS.md)
- [Staged Changes](Documentation/engineering/STAGED_CHANGES.md)
- [Intent Model](Documentation/engineering/INTENT_MODEL.md)
- [Software Architecture](Documentation/engineering/SOFTWARE_ARCHITECTURE.md)
- [Identity and Storage Contracts](Documentation/engineering/IDENTITY_AND_STORAGE.md)
- [Protocol and Pairing](Documentation/engineering/PROTOCOL_AND_PAIRING.md)
- [Hardware Reference](Documentation/engineering/HARDWARE_REFERENCE.md)
- [Verification Backlog](Documentation/engineering/VERIFICATION_BACKLOG.md)
- [Accessibility Specification](Documentation/engineering/ACCESSIBILITY.md)
- [Legal/IP Working Reference](Documentation/legal/README.md)

Near-term unimplemented work belongs in `Documentation/engineering/STAGED_CHANGES.md`. Historical implementations should remain documented as history rather than being mistaken for the current ESP32 runtime.

## Status

TurnHub remains under active prototype development. Hardware, APIs, storage formats, pairing behavior, and UX may change until the applicable interfaces are explicitly frozen and verified.
