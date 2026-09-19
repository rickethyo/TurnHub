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
- [ ] Add and assign GPIO for the planned Pair button.
- [ ] Add and assign GPIO for the planned auxiliary Action/Win button.
- [ ] Revisit whether the original Action long-press remains after the auxiliary control exists.

## Wireless and pairing

- [ ] Select the production Sigil transport. ESP-NOW is currently an implemented experiment, not the frozen product decision.
- [ ] Compare BLE and other local/offline options against latency, power, OTA, multi-Sigil count, venue interference, and implementation complexity.
- [ ] Define first-time pairing UX on Atlas.
- [ ] Define first-time pairing UX on Sigil.
- [ ] Define re-pair/unpair behavior.
- [ ] Define factory-reset behavior for pairing data.
- [ ] Define stable device-ID generation/storage.
- [ ] Define how Atlas identifies itself to paired Sigils.
- [ ] Decide whether pairing requires cryptographic authentication in prototype, production, or both.

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

- [ ] Audit current Atlas migration code for any browser/Sigil logic that duplicates game-engine rules.
- [x] Route running-game PASS requests from physical Sigils, browser controls, and the Atlas master button through the shared authoritative `IntentDispatcher`.
- [ ] Investigate inconsistent PASS grace timing observed on hardware. PASS logging now records `ORIGIN` at request, cancel, reject, and commit so the affected input path can be identified without guessing.
- [ ] Migrate Pause/Resume through authoritative Intent handlers and remove controller-specific state mutation.
- [ ] Migrate Concede through an authoritative Intent handler.
- [ ] Migrate ClaimWin/ConfirmWin/DenyWin through authoritative Intent handlers.
- [ ] Migrate lobby/lifecycle actions through Intent handlers where they represent semantic requests rather than local input gestures.
- [ ] Make physical and virtual Sigils use the same semantic action layer where practical.
- [ ] Define a controller interface suitable for a simulator/test harness.
- [ ] Add repeatable multi-player simulation scenarios.
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

Last reconstructed: 2026-09-19
