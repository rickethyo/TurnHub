# Atlas-Sigil Radio Protocol

The ESP-NOW contract between Atlas and its Sigils. The single source is
`shared/include/protocol.h` (Invariant 4): both PlatformIO projects and the
host test runners add `-I../shared/include`. Pairing and encryption are in
[Pairing and Secure Link](PAIRING_AND_SECURE_LINK.md); the HTTP client
contract is `protocol/http-v1.md`.

## Basics

- **Transport:** ESP-NOW on Wi-Fi channel 6, at most eight Sigils per Atlas.
  ESP-NOW is the prototype transport; the production transport is still an
  open decision.
- **Protocol `VERSION` 3** (2026-09-30). Every board runs the same version;
  a handshake, update offer or update status from another version is
  refused. Changing `protocol.h` means reflashing Atlas and every Sigil
  together. Backward compatibility comes back only for released hardware
  (what it needs is listed in [Firmware Updates](FIRMWARE_UPDATES.md),
  "Version rules").
- **Sealed.** After pairing, every packet in both directions travels inside
  the Secure Link envelope (AES-128-CCM, +15 bytes). Only `PairRequest2`,
  `PairRequestSpare` and `PairAccept2` are cleartext.
- **Packed structs.** Host tests check the sizes: the 7-byte control
  `Packet` and the 110-byte `GameDisplay`. The GCC runner's
  `-mno-ms-bitfields` keeps the layout.
- **Self-healing.** A Sigil sends Hello every 2 s. Atlas answers each Hello by
  resending the light, menu, screen, life request, seat colors and other
  state, so a lost packet or a quiet reboot heals within one Hello. There is
  no application-level acknowledgement protocol.
- **Atlas lost.** A paired Sigil that hears nothing valid from its Atlas for
  `LINK_TIMEOUT_MS` (7 s; the same silence after which Atlas marks a Sigil
  offline) shows **Atlas lost / Searching for Atlas**, an orange sweeping
  light (two steady pixels under Reduced motion), and drops its stale menu,
  unsent life change and life request. Only the Device menu and Pair work.
  The first valid packet restores the screen (`Sigil/include/atlas_link.h`).

## The baseline Sigil and capability bits

Every Sigil is built the same way (owner, 2026-09-30), so Atlas assumes the
baseline and the Hello capability byte carries only what varies. Baseline:
a screen for the player and game (`GameDisplay`), five-key input (thumbstick)
with Atlas's action menu (`MenuState2`/`SelectAction`), the profile picker,
life keys, adjustable hold times (`InputTiming`) and the NeoPixel Jewel ring
drawn from `LedState`. Atlas reads the byte through `helloCapabilities()`.

| Bit | Name | Meaning |
|---|---|---|
| 0x10 | `CAPABILITY_DISPLAY_OLED` | OLED display; clear means e-paper. Also picks the OTA package. On a spare it reports the GPIO4 strap |
| 0x40 | `CAPABILITY_SPARE` | Running the inert spare firmware ([Firmware Updates](FIRMWARE_UPDATES.md), "Spare Sigils") |
| 0x80 | `CAPABILITY_HARNESS` | The retired hardware test harness |
| 0x01, 0x02, 0x04, 0x08, 0x20 | free | Take one for a new meaning (battery, haptics, "can sleep") with the protocol version that introduces it |

## Packets

Direction A→S is Atlas to Sigil. Numbers 3-8, 10-11, 20-22 and 26 belonged to
retired packets and are free again.

| Type | # | Dir | Purpose |
|---|---|---|---|
| `Hello` / `Ack` | 1 / 2 | S→A / A→S | Keep-alive with firmware version and capabilities (`encodeHelloInfo`) |
| `DisplayProfileRequest`, `DisplayState`, `DisplayNameChunk` | 9, 30, 31 | both | Lobby, pairing, pause and game-over screens and seat names |
| `GameDisplay` | 32 | A→S | Running-game screen: focused and secondary player (name, life), up to three Commander damage sources and a `+N` overflow. One complete 110-byte datagram, so loss never mixes fields from two updates |
| `Unpair` | 12 | A→S | Atlas forgot this Sigil; honored only from its saved Atlas |
| `SelectAction` | 13 | S→A | The chosen menu action and menu revision. Bits 11-15 carry the receiving player for Commander actions |
| `MenuState2` | 34 | A→S | Mask of the 29 actions this Sigil may use now, plus a 3-bit revision. Atlas drops a choice from an old revision |
| `ProfilePicker` / `PickerKey` | 33 / 15 | A→S / S→A | Profile picker page (51 bytes: mode, notice, page, three names) and the key pressed on it, with the page revision |
| `LifeAdjust` | 16 | S→A | Batched life change, sent `LIFE_ADJUST_COMMIT_MS` (2 s) after the last press |
| `LifeRequest` / `LifeResponse` | 35 / 17 | A→S / S→A | Another player's pending life request and the answer, matched by a 6-bit tag |
| `Buzzer` | 23 | A→S | Notes for the Sigil's buzzer |
| `InputTiming` | 24 | A→S | Long-press and win-hold thresholds from the seated players' accessibility settings; limits and `validInputTiming()` are shared |
| `LedState` | 25 | A→S | The light's meaning: cue, overlays, player number, seat, shared flag, light style and phase. The Sigil draws it |
| `TableClock` | 39 | A→S | Atlas's clock every 2 s, so every Sigil's looping patterns run in step |
| `SeatColor` | 36 | A→S | A seated profile's chosen Jewel color, used only for calm Joined/Waiting cues |
| `StartingLife` | 37 | A→S | Starting life of the running game, for the life dial and 100-point steps |
| `PassPending` | 38 | A→S | Which player's pass is in its grace period, so the whole table sees it |
| `UpdateNotice` | 49 | A→S | Update available: 0 none, 1 Atlas only, 2 a Sigil |
| `CommanderFlow` / `CommanderKey` | 50 / 51 | A→S / S→A | Commander damage entry pages (80 bytes) and keys, tagged with the page revision |
| `FactoryReset` | 28 | A→S | Erase NVS and restart; needs `FACTORY_RESET_CONFIRM` ("FRES") from the saved Atlas for this Sigil's ID |
| `PairRequest2`, `PairAccept2`, `PairConfirmed`, `PairRejected`, `PairRequestSpare` | 40-43, 52 | both | Pairing ([Pairing and Secure Link](PAIRING_AND_SECURE_LINK.md)) |
| `SecureHello`, `SecureHelloAck`, `Secure` | 44-46 | both | Session start and the sealed envelope |
| `SigilUpdateOffer` / `SigilUpdateStatus` | 47 / 48 | A→S / S→A | Firmware update ([Firmware Updates](FIRMWARE_UPDATES.md)) |
| `HarnessCommand` / `HarnessReport` | 27 / 14 | both | Retired test harness; Sigils ignore them |

Display flag `DISPLAY_FLAG_PRIMARY_B` (bit 0x10, the old host bit) marks
physical seat B, so Sigils name seats by physical identity even when B plays
before A.

## Behavior notes

- **Menus.** Atlas works out which actions a Sigil may use from table state
  (`Atlas/src/sigil_menu.cpp`) and sends `MenuState2` on change. The Sigil
  lays them out (`SigilMenu`; the layout is in `Sigil/DISPLAY.md`, "Menus")
  and times holds itself with the seated players' thresholds. Atlas
  dispatches the same Intents a phone would (`handleSelectAction` in
  `sigil_input.cpp`). Device entries (Sleep, Unpair, Factory reset) never go
  to Atlas.
- **Life.** Left/Right change the shown seat's life when `AdjustLife` is
  offered (a living seat, running or paused game, no claim or elimination).
  A Sigil steps by 100 in a game that starts at 1000 or more.
- **Lights.** Atlas picks every cue (`selectSigilLedState`); the Sigil only
  draws it (`Sigil/src/sigil_led.cpp`): player number as lit pixels, a
  shared seat as its half of the ring, the top overlay in the center. Pairing
  blink, hold progress and the pass flash stay Sigil-local.
- **Display snapshots.** Atlas compares snapshots before sending; the Sigil
  compares received and rendered snapshots, so identical resends never
  refresh the e-paper. There is no periodic e-ink refresh.
- **Atlas lost recovery.** After an Atlas restart the old session is gone;
  the Sigil sends a new SecureHello within about 4-6 s, before the 7 s Atlas
  lost screen.

## Still to decide for production

Which messages need acknowledgement or deduplication IDs, retry timing,
ordering guarantees, the transport itself (ESP-NOW versus BLE or another
local link), the maximum Sigil count, and version negotiation once released
hardware needs backward compatibility. These are in
[Staged Changes](STAGED_CHANGES.md), "Production decisions".

History: the Generation 1 wired serial protocol (`PASS|1`, `BLUE|1|<0-255>`)
is in [Generation History](history/GENERATION_HISTORY.md). The first ESP-NOW
builds paired by proximity; explicit pairing replaced that on 2026-09-22, the
Secure Link on 2026-09-29, and protocol 3 dropped every compatibility path on
2026-09-30.
