# Hardware: Boards, Pins, Screens and Flashing

Master's build targets and the tools that flash them.
Earlier generations are in [Generation History](history/GENERATION_HISTORY.md).
> **Branches (2026-10-13).** The Gen 1 ESP32 boards (E32R28T Atlas, e-ink and
> OLED Sigils), their drivers, the Sigil carrier PCB, the 3D enclosures and the
> board setup/flash scripts live on the `legacy/gen1` branch; fix Gen 1 bugs
> there. Master builds board-less until the S3 boards
> ([ESP32-S3 refresh](hardware/ESP32_S3_REFRESH.md)) land.

## Hardware development direction (owner, 2026-10-10)

Gen 1 ESP32 DevKit e-paper and OLED Sigil feature development is frozen (`legacy/gen1`).
Their accepted firmware, radio contract and 16-pixel preset assets remain the
baseline. New screen work targets planned ESP32-S3 LCD hardware with 2.8-inch
and 4-inch displays. Candidates are now LCDWIKI ES3C40P (Atlas 320×480 portrait) and ES3C28P
(Sigil 240×320 portrait). [S3 hardware planning](hardware/ESP32_S3_REFRESH.md)
owns source-verified specifications, pin conflicts, electrical/mechanical holds
and [prototype BOM](hardware/PROTOTYPE_BOM.md). Physical compatibility and prices
remain open; neither S3 firmware target exists yet.

The [LVGL desktop foundation](../../LVGL/README.md) renders both candidate profiles
with existing themes/assets and simulated pointer/five-way input. This is an
isolated experimental renderer; no S3 firmware environment or GPIO binding exists.

Player artwork begins independently of those ports: vector defaults, 64-pixel
Atlas alpha masks, and square JPEG masters up to 512 pixels/48 KiB stored on
Atlas's SD card. Android previews provisional 64/128-pixel icon slots for the
2.8/4-inch layouts. Inch size does not determine pixel resolution. The Gen 1
Atlas rendered a small approved RGB332 thumbnail in a 16-pixel slot; the S3
layouts consume the master rather than enlarging that thumbnail.

## Master's board-less builds (2026-10-13)

Master no longer drives the Gen 1 boards. Their hardware pages (the E32R28T
Atlas TFT, touch calibration, speaker, battery wiring, the e-ink and OLED
Sigils, themes on those panels) are on `legacy/gen1`. Until the S3 boards
land, master builds for a generic 4 MB ESP32 dev board so the engine, radio,
storage and HTTP API keep compiling for the target:

- **Atlas** (`pio run -e atlas`, `min_spiffs.csv`: two 1.9 MB OTA slots, NVS
  at `0x9000`). The board boundary is `atlas_board.h`, `atlas_display.h`,
  `atlas_battery.h` and `atlas_speaker.h`, implemented by
  `atlas_board_none.cpp`: no screen, no status LED, no speaker, no battery
  (`present` false). It keeps the saved theme and inversion (`display` NVS
  namespace) for the next screen. BOOT (GPIO 0) pairs, unpairs all and
  factory resets (quick, 3 s, 10 s) and wakes from sleep; the microSD card
  uses the default VSPI pins (18/23/19, CS 5). The touchscreen's screen model
  and buttons (`touch_controls.cpp`) stay host-tested for the S3 renderer.
- **Sigil** (`pio run -e sigil`, alias `sigil-e32r28t`, `huge_app.csv`):
  touchscreen-only E32R28T target with a 240 × 320 portrait ILI9341,
  XPT2046 resistive touch and LVGL. Board pins and drivers are in
  `Sigil/include/sigil_board.h` and `Sigil/src/sigil_board_e32r28t.cpp`.
  No LED/ring output, analog joystick or headless display fallback.
  E-ink/OLED firmware is maintained only on `legacy/gen1`.

**Touchscreen navigation:** both Atlas S3 and the touchscreen Sigil open on
Home, with persistent **Home / Game / Table / Settings** destinations. Pairing
codes and update progress temporarily take priority over navigation. Atlas Game
retains its host controls; Actions holds master pass and End match. Table is a
read-only roster; browsing its tiles changes no player or match state. Settings
stays available during play, with destructive Atlas device actions hidden then.

**Sigil controls:** Game follows the personal Share Play panel: a large life
total with direct minus/plus buttons, a primary action and More for the other
Atlas-offered actions. Pending life changes preview the total. Profiles are
full-width touch rows; menu actions are touch tiles. Only visible controls have
hit regions. A drag, changed action, changed player or changed request cancels
a contact until release. Atlas validates every request. Hold on-screen Pair for
3 seconds while unpaired, then compare the code with Atlas. Settings includes
theme, inversion, touch calibration, held Unpair and held Factory reset.

The Sigil Table page receives an encrypted read-only roster from its Atlas game:
up to 16 players, names, life, turn/out/winner/starter labels and local paging.
Two radio pages form one revision; incomplete pages never mix into the visible
roster. No profile IDs, PINs, private account data or game rules are sent.
The last roster remains visible with an offline label after disconnection.
BOOT remains quick-press pairing, 3-second unpair and 10-second factory reset.
Sleep is removed for now; Sigil stays awake while powered. Speaker cues use
GPIO 26 with active-low amplifier enable GPIO 4. Upload over USB; S3 Sigil
hardware and OTA integration remain staged.

## Identifying and flashing boards

Never hard-code COM numbers; they change whenever the PC restarts. Boards are
identified by factory MAC:

```
pio pkg exec -p tool-esptoolpy -- esptool.py --port COMx read_mac
```

(Reading the MAC resets the board.) Each PC keeps its own list in the
git-ignored `tools\boards.local.md`; never put real boards in a committed
file. Its format, with made-up MACs (`02:` is locally administered, so no
real board has one):

| Board | Firmware | USB bridge | MAC | Notes |
|---|---|---|---|---|
| Atlas | `atlas` | CH340 | `02:00:00:00:00:01` | Its MAC is its `THA-` ID |
| Sigil | `sigil` | CP210x | `02:00:00:00:00:02` | |

Flash with `pio run -e <env> --target upload --upload-port COMx`. The Gen 1
board setup and flash-all scripts are on `legacy/gen1`; the signing tools are
described in `CLAUDE.md`.

## Rules and open items

- **Hardware stops at the hardware boundary.** GPIO numbers, polarity and
  driver quirks never reach game rules; hardware code turns physical input
  into semantic events. A PCB revision must be able to move a pin without
  touching the engine.
- **Revision names:** `PROTO-G2-ESP` for the Gen 1 boards. Reserve
  `ATLAS-REV-A` / `SIGIL-REV-A` for the first reproducible schematic + BOM,
  not a breadboard that happens to work.
- **Open:** the S3 boards' pin maps, power measurements (idle, radio,
  display, buzzer, capped LEDs, sleep) and USB supply behavior, Atlas battery
  hardware, enclosure and docking decisions. Tracked in [Staged Changes](STAGED_CHANGES.md).
  A first, unprinted phone-style ES3C40P case is in
  [`Atlas/enclosure/s3_phone_case/`](../../Atlas/enclosure/s3_phone_case/README.md) (*Experimental*).
  A matching case for the E32R28T board as a Sigil stand-in is in
  [`Sigil/enclosure/e32r28t_case/`](../../Sigil/enclosure/e32r28t_case/README.md) (*Experimental*).
