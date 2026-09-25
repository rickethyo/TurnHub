# TurnHub Engineering Verification Backlog

This file exists so provisional documentation can be useful immediately without silently becoming permanent fact.

When an item is verified, update the relevant reference document and mark the item complete here with the evidence used.

## Historical hardware

- [ ] Identify the exact board used for the first standalone TurnHub prototype.
- [ ] Recover the earliest known pinout for the Generation 0 prototype.
- [ ] Confirm the exact potentiometer wiring and timer range from the earliest adjustable-timer version.
- [ ] Confirm when the DIP-switch timer concept replaced or supplemented the potentiometer.
- [ ] Verify the earliest buzzer pin and hardware type.
- [ ] Verify the original two-player LED wiring and whether any red/green inversion existed before the Pi generation.
- [ ] Photograph and label surviving Generation 0 hardware if still available.

## Raspberry Pi / wired generation

- [ ] Confirm the Raspberry Pi model/revision used throughout the wired phase. Current project history identifies a Raspberry Pi 3B.
- [ ] Recover and document the wired ESP32 Module 0 pinout.
- [ ] Confirm whether Module 0 and Module 1 used identical logical controls for the entire wired phase.
- [ ] Document the exact USB/serial topology used between Pi and modules.
- [ ] Confirm when automatic pause-on-module-disconnect and reconnect recovery were introduced.
- [ ] Record the historical red/green inversion source in hardware if it can still be physically traced.

## Current Atlas development hardware

- [ ] Record the exact development Atlas board manufacturer/model, not only the generic PlatformIO `esp32dev` target.
- [ ] Produce a physical header-to-GPIO pin map for that board.
- [ ] Confirm available safe GPIOs before assigning pairing/status/auxiliary hardware.
- [ ] Decide whether the prototype/final Atlas keeps separate USB-C power and firmware/data ports.
- [ ] Document regulator/input-voltage requirements.
- [ ] Determine whether production Atlas requires battery/backup power.
- [ ] Decide whether production authenticity uses a secure element and, if so, select the device and interface.

## Current Sigil development hardware

- [ ] Record the exact Sigil development board manufacturer/model.
- [ ] Verify every current GPIO assignment against the physical breadboard.
- [ ] Record the SPI clock/data pins used by the e-ink library/board defaults in addition to CS/DC/RST/BUSY.
- [ ] Record the exact e-ink panel seller/model/revision and confirm it matches the current GxEPD2 target.
- [ ] Verify portrait rotation and final usable resolution.
- [ ] Measure full-refresh and partial-refresh behavior on the physical panel.
- [ ] Measure approximate Sigil current draw during idle, radio activity, buzzer operation, and display refresh.
- [ ] Decide final battery chemistry/form factor for portable Sigils.
- [ ] Decide whether charging is onboard, external, or battery-swap based.
- [x] Add and assign GPIO for the Pair button (GPIO19, verified working firmware). Since 2026-09-25 the real E-ink Sigil uses the DevKit BOOT button (GPIO0) for Pair; GPIO19 remains only in the Wokwi build.
- [x] Add and assign GPIO for the auxiliary Pause / Win button (GPIO32; bench check pending).
- [x] Verify the auxiliary path maps to semantic Atlas actions and introduces no
  button-owned game rules (it reuses Action-long and Action-win).
- [ ] Revisit whether the original Action long-press remains after the auxiliary control exists.

## Wireless and pairing

- [ ] Select the production Sigil transport. ESP-NOW is currently an implemented experiment, not the frozen product decision.
- [ ] Compare BLE and other local/offline options against latency, power, OTA, multi-Sigil count, venue interference, and implementation complexity.
- [ ] Define first-time pairing UX on Atlas.
- [ ] Define first-time pairing UX on Sigil.
- [x] Define re-pair/unpair behavior. See [Manual Pairing](MANUAL_PAIRING.md#forgetting-a-pairing-2026-09-24); bench acceptance pending.
- [ ] Define factory-reset behavior for pairing data.
- [ ] Define stable device-ID generation/storage.
- [ ] Define how Atlas identifies itself to paired Sigils.
- [ ] Decide whether pairing requires cryptographic authentication in prototype, production, or both.
- [x] Prototype 1.0: replace passive proximity adoption with one Atlas-owned pairing
  state machine using a 15-second window. See [Manual Pairing](MANUAL_PAIRING.md);
  radio bench acceptance is still pending.
- [x] ~~Prototype 1.0: temporary boot-triggered pairing~~ - superseded; the Pair
  button was wired first, so no boot trigger was built.
- [ ] Verify paired startup reconnects only to the retained Atlas relationship and
  that re-pair/forget paths do not silently adopt a neighboring Atlas.

## Protocol

- [ ] Freeze semantic event names independently from transport encoding.
- [ ] Define protocol version negotiation.
- [ ] Define capability flags.
- [ ] Define acknowledgement/retry requirements.
- [ ] Define event deduplication strategy for user actions such as Pass.
- [ ] Define reconnect/state-resynchronization behavior.
- [ ] Define maximum supported physical Sigil count for Gen 1 product hardware.
- [ ] Define two-player-per-Sigil support as either production feature, optional mode, or prototype-only capability.
- [ ] Define firmware-update compatibility messages.

## Software architecture

- [x] Define identity/persistence ownership and introduce an NVS-backed statistics
  storage boundary preserving existing data. Atlas build, native gameplay/storage
  tests and adapter audit pass; see [contracts](IDENTITY_AND_STORAGE.md).
- [ ] Physically verify the new storage path preserves an existing profile,
  PIN/name and totals across update/reboot, and records one completed game once.

- [x] Audit current Atlas migration code for any browser/Sigil logic that duplicates game-engine rules. See [Atlas intent verification](ATLAS_INTENT_VERIFICATION.md) for source audit/native test evidence; hardware regression remains pending.
- [x] Route running-game PASS requests from physical Sigils, browser controls, and Atlas's physical control (the master button then; since 2026-09-24 the touchscreen Pass button) through the shared authoritative `IntentDispatcher`.
- [x] Investigate inconsistent PASS grace timing observed on hardware. The 2026-09-19 ESP32 Build Fix conversation reports three-second physical/browser pending-to-commit timing and Action cancellation on the pre-migration build. New native scenarios verify grace/cancellation/rollover; repeat the hardware test after this migration.
- [x] Eliminate optional Preferences/NVS NOT_FOUND spam without reducing error logging. Native fault-injection policy tests and clean firmware build pass; see [verification record](ATLAS_INTENT_VERIFICATION.md).
- [x] User reports gameplay migration checks tested after the supplied serial run.
  The migration is now merged into master; on 2026-09-20 the owner also accepted
  the repository build/current hardware baseline. See the
  [post-migration hardware checklist](ATLAS_INTENT_VERIFICATION.md#hardware-verification-still-required).
- [x] Migrate Pause/Resume through authoritative Intent handlers and remove controller-specific state mutation. See [Atlas intent verification](ATLAS_INTENT_VERIFICATION.md) for source audit/native test evidence; hardware regression remains pending.
- [x] Migrate Concede through an authoritative Intent handler. See [Atlas intent verification](ATLAS_INTENT_VERIFICATION.md) for source audit/native test evidence; hardware regression remains pending.
- [x] Migrate ClaimWin/ConfirmWin/DenyWin through authoritative Intent handlers. See [Atlas intent verification](ATLAS_INTENT_VERIFICATION.md) for source audit/native test evidence; hardware regression remains pending.
- [x] Migrate lobby/lifecycle actions through Intent handlers where they represent semantic requests rather than local input gestures. See [Atlas intent verification](ATLAS_INTENT_VERIFICATION.md) for source audit/native test evidence; hardware regression remains pending.
- [x] Phone-only and mixed participation use the same Intent handlers; profile
  sessions resolve to a single participant shared with physical input. Native
  HTTP/application regression checks pass; `0.6.0-dev` bench acceptance is pending.
- [ ] Bench-check `0.6.0-dev`: old profiles, two-phone game with Sigils off,
  concurrent phone/Sigil control, statistics once and persistence after reboot.
- [ ] Prototype 1.0 recovery: define a compact versioned active-match record rather
  than persisting the in-memory engine object.
- [ ] Prototype 1.0 recovery: allocate/persist MatchId and a completion receipt or
  equivalent idempotency marker before automatic replay of completion/stat updates.
- [ ] Prototype 1.0 recovery: save only after accepted semantic state transitions;
  verify timer/display ticks do not create continuous flash writes.
- [ ] Prototype 1.0 recovery: abrupt-power test from lobby, running, paused,
  mid-turn, after pass, after concession, and around game completion. A recovered
  match must open paused and must not charge downtime to a player.
- [ ] Prototype 1.0 recovery (Discard = 5 s End match draw hold on the Atlas touchscreen, 2026-09-24): verify Resume/Discard, re-login, physical/controller
  reattachment, corrupt/unsupported snapshot rejection, and exactly-once statistics.
- [ ] Define a controller interface suitable for a simulator/test harness.
- [x] Add repeatable multi-player simulation scenarios. See [Atlas intent verification](ATLAS_INTENT_VERIFICATION.md) for source audit/native test evidence; hardware regression remains pending.
- [ ] Review timer implementation for timestamp/derived-state behavior rather than unnecessary repeated state mutation.
- [ ] Review e-ink update code for state-change/dirty-region opportunities.
- [ ] Define the authoritative persistence format for paired devices separately from player profiles.

## OTA

- [ ] Re-test Atlas OTA after the recent migration issue where upload appeared accepted but application was uncertain.
- [ ] Confirm update validation before reboot.
- [ ] Define rollback/recovery behavior after failed Atlas update.
- [ ] Choose Sigil OTA transport and architecture.
- [ ] Define Atlas-to-Sigil update orchestration if Atlas distributes Sigil firmware.
- [ ] Define hardware-revision compatibility checks before flashing.

## Mechanical/manufacturing

- [ ] Create first reproducible Atlas electrical revision before assigning `ATLAS-REV-A`.
- [ ] Create first reproducible Sigil electrical revision before assigning `SIGIL-REV-A`.
- [ ] Create BOMs for both Rev A designs.
- [ ] Create wiring/schematic diagrams tied to each revision.
- [ ] Define enclosure mounting points around actual PCB geometry.
- [ ] Define docking/contact geometry only after power and connector decisions stabilize.
- [ ] Review design-for-assembly and repair with the intended small-batch PCBA manufacturer.

## Documentation process

- [ ] Add photos of each surviving prototype generation.
- [ ] Add dated architecture diagrams for each generation.
- [ ] Add a formal decision log for major product decisions and reversals.
- [ ] Tag future docs with hardware revision and firmware/protocol version where applicable.
- [ ] Update this backlog whenever a provisional claim is added elsewhere.

Last updated: 2026-09-21
