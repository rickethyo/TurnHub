# LVGL presentation foundation

**Experimental**. Firmware status (2026-10-28): `env:atlas-s3` runs LVGL 9.3
on the S3 Atlas, drawing the existing `touch_controls.cpp` screen model
(`atlas_display_s3.cpp`); the presentation types below are not bound yet.
Hardware/BOM planning completed first in
[the S3 refresh](hardware/ESP32_S3_REFRESH.md). `LVGL/` is isolated from Atlas,
Sigil and their PlatformIO dependencies. This milestone is a UI foundation,
not S3 firmware or a second playable game engine. Build/status belongs in
[LVGL/README.md](../../LVGL/README.md).

## Feature gate

| Question | Boundary |
|---|---|
| State owner | Atlas retains canonical engine, identity, profiles, seats, statistics and two-game ownership. Simulator fixture source supplies explicitly synthetic snapshots only. |
| Actions | Presentation emits semantic requests (`AdjustLife`, pause request, mock pairing); local navigation/theme choices stay presentation state. Production adapter must map requests to existing Atlas Intents/Sigil semantic events, not mutate engine from a widget. |
| Validator | Existing Atlas Intent handlers in future firmware. Simulator accepts bounded display demonstrations only; no win/elimination/statistics/authorization implementation. |
| Persistence | None in simulator. Existing Atlas NVS/SD and device appearance ownership remain; no new player/profile database. |
| Rendering | Shared LVGL view receives immutable snapshots; profile decides dimensions/density. Hardware display flush, touch and navigation are adapters. |
| Contract | No radio/HTTP changes. Snapshot/action C++ types are internal presentation types, not a new wire format. No production binding until mapping is reviewed. |
| Dependencies | Pin LVGL 9.3.0 + SDL 2.32.4 for desktop; update legal inventory/notices. Future firmware driver evaluation is separate. |
| Accessibility | Redundant TURN/OUT/PAUSED/WINNER words, focus outline, keypad and pointer access, theme switching, no required animation. Touch targets aim at existing token 44 px minimum. No new hold-only recovery UI in the simulator. |

## Layers

```text
Existing Atlas engine / authoritative client state
                ↓ future snapshot adapter (no rules)
Presentation snapshot ← desktop deterministic fixture provider
                ↓
Shared LVGL widgets ← local screen/theme/player selection
                ↑
Semantic action sink → future existing Intent/event path
                ↑
Touch pointer / keypad input ← SDL now; FT6336 / PCA9554 later

LVGL display → SDL backend now; per-board SPI flush + DMA later
Asset provider → existing avatar masks now; approved SD artwork later
```

Keep one LVGL-owning task. Network/card tasks enqueue snapshots or asset completion,
not widget calls. UI callbacks enqueue actions, not blocking transport/storage.
Presentation snapshots copy bounded display facts, using participant/profile IDs
when connected; never infer identity from names or hardware MAC. Atlas adapter
must retain TableScope/game selection. Sigil adapter renders authoritative radio
state and derived timer anchors if supplied; do not expand packets speculatively.

## Versions and firmware options

Use a fixed stable **LVGL 9.3.0** release/commit for the first desktop milestone,
with upstream supported SDL display/mouse driver. Version 9 provides maintained
input/display APIs and a direct firmware migration path; do not mix vendor 8.x
examples with 9.x headers. SDL **2.32.4** is pinned for desktop only. CMake fetches
content by SHA256; Python generators read existing tokens/assets, not network art.
Do not replace the legacy Arduino core (2.0.17) to obtain these dependencies.

Future S3 options: Arduino + separately pinned LovyanGFX SPI panel and custom LVGL
flush/touch adapter, or ESP-IDF `esp_lcd` plus `esp_lvgl_port` and a compatible
ST7796/ILI9341 component. Both panels are **SPI**, not an RGB-parallel framebuffer
board. Verify exact library release/core compatibility and DMA buffer placement
on sample boards before adding separate `atlas-s3` / `sigil-s3` environments.
Manufacturer Arduino demo instructions name Arduino IDE 2.3.4, Arduino-ESP32
3.2.0, TFT_eSPI and **LVGL 8.3 only, explicitly excluding 9.x**. Therefore
this 9.3 foundation needs new display/touch adapters; vendor example compatibility
is not claimed. ESP-IDF instructions are offered but their exact compatible
component versions remain unverified. This is not TurnHub firmware acceptance. Prefer one graphics driver path for both boards,
with explicit differing controller/pin/power profiles. Do not copy vendor demos
or their unexplored dependency versions into production.

At RGB565 full frame sizes are Atlas 307,200 B and Sigil 153,600 B; double buffering
is 614,400 / 307,200 B. At RGB888 the frames are 460,800 / 230,400 B. Start firmware
with partial DMA line buffers in internal DMA-capable RAM, and verified PSRAM
for larger caches; do not assume all PSRAM can be given directly to SPI DMA.
Measure frame transfer time, dirty areas, memory under AP + ESP-NOW + portal load,
and watchdog/latency before selecting buffering. An S3 is not a proven fix for
current multi-phone stability.

## Visual identity and assets

Consume generated `shared/include/device_theme.h`, whose source is
`design/tokens.json`: Graphite, Daylight, Brass and High contrast, shared between
profiles. Reuse flat cards, gold accents and teal turn emphasis; Brass keeps its
ornament and serif direction. Runtime selection is local/disposable in desktop.
Fonts should derive from checked-in Inter/Cinzel (OFL) with pinned conversion
and digest tracking; bundled LVGL fonts are a fallback, not a new brand choice.

The asset interface supplies existing `Atlas/include/avatar_masks.h` 64px alpha
masks (from `design/avatars/*.svg`), with representative preset fallback. It does
not implement a new upload, approval, profile or avatar store. Existing square
JPEG masters are up to 512px / 48 KiB and Atlas-owned SD publication/approval
remains authoritative. JPEG decoding and SD artwork loading are future work.
A 512px decoded RGB888 image needs 786,432 B; RGB565 524,288 B, far above its
compressed size. A 64px RGB565 derivative is 8,192 B, 128px is 32,768 B. Budget
bounded derivatives, asynchronous reads through the existing SD lock, cache keys
including asset revision, approval, removal and missing-card fallback. Never
fetch an image synchronously in a widget callback or send masters in game packets.
Sigil artwork transport/cache policy is still pending; SD is not required for play.

## Recovery and later migration

Touch will own pairing/unpairing, setup, settings/themes, player selection,
life and menus. Five-way controls remain a convenient gameplay interface and
map into the same semantic action sink. No separate Pair switch. Keep onboard
BOOT+RESET accessible for ROM USB download even if touch/rendering is broken;
implement deliberate long-hold factory-reset recovery independently of LVGL
with existing authorization/data-reset semantics when firmware is ported.

Existing signed `.thfw` products, descriptors, rollback checks and OTA slot layouts
remain unchanged. New S3 images need deliberate distinct product/board identity,
verified partition sizing, signature/product rejection and USB recovery tests;
never label an S3 image as a legacy Atlas/Sigil package. See
[Firmware updates](FIRMWARE_UPDATES.md). No S3 OTA integration in this milestone.

Next: measure hardware and resolve pin/power contradictions; implement only the
verified per-board display/touch/keypad drivers; bind snapshots/actions to existing
production paths with host tests; then artwork, production menus and OTA. Do not
replace either live renderer until those independent targets are accepted.
