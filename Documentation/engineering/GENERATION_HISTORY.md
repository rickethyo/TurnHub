# TurnHub Generation History

This document reconstructs TurnHub development in generations. Generation boundaries are practical engineering milestones, not formal commercial revisions.

## Generation 0 - Standalone turn-timer proof of concept

**Status:** Reconstructed

### Product goal

Prove that a tabletop device could manage turn timing and communicate game state physically without requiring a phone.

### Hardware

- Arduino-class microcontroller prototype.
- Physical Pass and Action controls.
- Red, green, and blue status indication.
- Piezo/buzzer feedback added during prototype development.
- Early timer adjustment used a potentiometer; later experiments moved toward fixed timer modes/DIP selection.

### Software behavior

- Two-player turn sequencing was the original scope.
- Short Pass interaction advanced the active player.
- Long Action interaction was used for pause/resume behavior.
- Extended holds and multi-press/hold concepts were explored for game-end/winner functions.
- LED states communicated active player, waiting state, pause, timer warning, and game end.

### Architectural characteristic

Game behavior and hardware behavior were comparatively close together. The primary question was whether the interaction model worked at all.

### What this generation proved

- Physical turn passing is understandable at the table.
- Turn warnings and status LEDs provide useful at-a-glance feedback.
- Long-press interactions can provide secondary functions without excessive controls.
- Audible feedback helps confirm actions without requiring players to look at the device.

### Items still to verify

- Exact first-day board model and pinout.
- Exact order in which buzzer, timer adjustment, pause, and winner behaviors were introduced.
- Exact potentiometer and DIP wiring used in the earliest prototype.

---

## Generation 1 - Raspberry Pi Atlas with wired modules

**Status:** Verified in repository, with some reconstructed hardware details

### Product goal

Move game-state authority away from individual player modules and prove that one central host could coordinate multiple physical controllers.

### Hardware

- Raspberry Pi 3B used as the central host, later named Atlas.
- Mixed microcontroller player modules, including an ESP32 module and Arduino module.
- Modules connected to the host over wired serial/USB during this phase.
- Physical modules used Pass and Action buttons plus red, green, and blue indicators.

A surviving Arduino Module 1 firmware file explicitly states:

- Raspberry Pi owns game state.
- Arduino Module 1 is a temporary wired player module.
- Blue LED: pin 5.
- Red LED: pin 6.
- Green LED: pin 3.
- Pass: pin 8.
- Action: pin 7.
- Serial: 9600 baud.

### Software

The central Python controller expanded into separate concerns for:

- Lobby construction.
- Game engine.
- Player state.
- Serial transport.
- LED rendering.
- Audio.
- Game logging.
- Hardware/status monitoring.

The central-host model became an important architectural rule: microcontrollers report inputs and render outputs, while the host makes game decisions.

### Transport

The wired protocol used human-readable serial messages such as:

- `READY|1`
- `PASS|1`
- `ACTION|1|SHORT`
- `ACTION|1|LONG`
- `ACTION|1|WIN`
- `BLUE|1|<brightness>`
- `RED|1|<state>`
- `GREEN|1|<state>`

### What this generation proved

- A central authoritative game engine works.
- Physical modules do not need to contain game-rule logic.
- Hardware can disconnect/reconnect without requiring the entire product concept to be redesigned.
- Multiple physical module types can coexist behind a common logical model.

### Known prototype artifact

The wired prototype had a red/green inversion below the software LED abstraction. The Python controller compensated for it at the serial boundary rather than infecting the game engine with hardware-specific color logic.

---

## Generation 1.5 - Pi-based tabletop platform and web UI

**Status:** Verified in repository and development history

### Product goal

Expand TurnHub from a physical timer into a local-first tabletop management platform without abandoning physical controls.

### Major software additions

- Local web portal.
- Virtual Sigils.
- Physical + virtual mixed tables.
- Player profiles.
- PIN-authenticated browser control.
- Avatars.
- Persistent settings and recoverable sessions.
- Life totals.
- Game profiles.
- Commander damage tracking.
- Cross-player life editing with reversible notifications.
- Concession/elimination.
- Victory claims requiring table confirmation.
- Nudge feedback.
- Statistics and game logs.
- Developer/system controls.

### Architectural milestone

The logical model became increasingly separated:

`Profile -> Player -> Controller -> Physical Sigil or Virtual Sigil`

That separation meant a player could change controller without changing the player's actual game state.

### What this generation proved

- TurnHub's useful scope extends beyond timing.
- Browser clients can coexist with physical hardware.
- A table can continue to function even when not every player has a physical module.
- Persistent player identity and game history fit naturally into the platform.

---

## Generation 2 - ESP32 Atlas/Sigil migration

**Status:** Current development generation

### Product goal

Remove the Raspberry Pi dependency and move toward a compact, dedicated, manufacturable hardware platform based on ESP32-class controllers.

### Atlas

Current migration code targets an `esp32dev` PlatformIO environment.

The ESP32 Atlas currently contains native modules for:

- Lobby.
- Game engine.
- LED rendering.
- Audio control.
- Sigil bus.
- Web API/pages.
- Player profiles and profile statistics.
- OTA manager.
- Persistent preferences.

The current development configuration uses:

- Master button: GPIO 32.
- Local access point SSID: `TurnHub-Atlas`.
- Current firmware HTTP port: 80.

### Sigil

Current Sigil firmware is also a PlatformIO ESP32 project and contains:

- Pass and Action inputs.
- Red/green/blue indicators.
- Buzzer.
- E-ink display support.
- Firmware version/capability reporting.
- Assigned/unassigned Sigil identity state.
- Profile/display synchronization.

Current implemented development pin assignments are recorded in the Hardware Reference.

### E-ink

The current software supports a 2.13-inch class black/white GxEPD2 display driver. Product direction has since moved toward using the screen in portrait orientation to keep the Sigil slim and provide better vertical information density.

### Wireless transport

The migration branch currently contains ESP-NOW transport code and a shared packet protocol. This is historically important and should remain documented as an implemented experiment.

However, ESP-NOW is **not considered the frozen product transport**. Current product direction is to avoid accidental proximity auto-discovery, introduce explicit pairing, and continue evaluating the most appropriate local wireless method for production Sigils.

### Product direction added during this generation

- Explicit Atlas/Sigil pairing.
- Stable device identities rather than positional module IDs alone.
- Pair button on Sigils.
- Additional physical control so common actions do not overload one button excessively.
- Portrait e-ink layout.
- Atlas-owned authoritative state.
- Shared logical protocol for physical and virtual controllers.
- OTA path for both Atlas and Sigils.
- Home and venue deployment concepts.

---

## Future production generation

**Status:** Planned

This generation has not been assigned a formal hardware revision yet. Expected changes include:

- Custom Atlas PCB.
- Custom Sigil PCB.
- Stable power architecture.
- Defined battery/charging strategy.
- Production connectors and docking.
- Locked display model and orientation.
- Hardware revision IDs.
- Stable pairing and update protocol.
- Enclosures designed for repair and assembly.
- Small-batch PCBA manufacturing.

The key goal is for production hardware to replace development boards without requiring a rewrite of the game engine or higher-level controller semantics.

Last reconstructed: 2026-09-19
