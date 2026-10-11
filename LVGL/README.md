# TurnHub LVGL desktop foundation

**Implemented experimental UI**, not ESP32-S3 firmware. Atlas **480×320 landscape**
and Sigil **240×320 portrait** match manufacturer native geometry after Atlas
rotation; physical orientation/touch transform still awaits hardware acceptance.
The live Atlas/DevKit Sigil renderers, game engine, protocols, PlatformIO and OTA
are unchanged. Architecture: [LVGL migration](../Documentation/engineering/LVGL_MIGRATION.md).
Hardware and purchasing: [S3 plan](../Documentation/engineering/hardware/ESP32_S3_REFRESH.md)
and [BOM](../Documentation/engineering/hardware/PROTOTYPE_BOM.md).

## Build (Linux)

Requirements: C/C++17 compiler, CMake **3.24+**, Ninja (or Make), network access
for initial dependency archives. Tested tool versions: GCC 14.2.0, CMake 3.31.6,
Ninja 1.11.1, Debian 13. Source dependencies are pinned with SHA256:

- **LVGL 9.3.0** commit `c033a98afddd65aaafeebea625382a94020fe4a7`, official SDL display/mouse backend.
- **SDL 2.32.4**, built from source by default. Software renderer, no Vulkan/OpenGL requirement.
- Fonts already generated from the repository's licensed Inter/Cinzel; Node/Python
  are only needed for regeneration/checking, not normal application compilation.

On Debian/Ubuntu, install the compiler/build and X11 development prerequisites
(with the system's package manager):

```sh
sudo apt-get install build-essential cmake ninja-build libx11-dev libxext-dev \
  libxrandr-dev libxcursor-dev libxi-dev libxfixes-dev libxss-dev libxinerama-dev
cmake -S LVGL -B LVGL/build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build LVGL/build --parallel 4
LVGL/build/bin/turnhub_sim --device atlas
LVGL/build/bin/turnhub_sim --device sigil --zoom 2
```

Run from the repository root. A desktop session/display is needed for visible
windows. The application works offline once dependencies have been fetched.
An initial LVGL archive is ~84 MB; retain `build/_deps` for subsequent builds.
No phone, hardware board, network server or audio device is required.

An optional installed-SDL route requires exactly **2.32.4** headers/libraries:

```sh
cmake -S LVGL -B LVGL/build-system -G Ninja -DTURNHUB_SYSTEM_SDL=ON \
  -DSDL2_DIR=/path/to/SDL2/cmake -DCMAKE_BUILD_TYPE=Debug
cmake --build LVGL/build-system --parallel 4
```

CMake's `FETCHCONTENT_SOURCE_DIR_LVGL` / `FETCHCONTENT_SOURCE_DIR_SDL2` options
may select previously extracted, hash-verified source archives for offline work.
Supplying those overrides bypasses CMake's download/hash step: verify the archives
against the committed CMake hashes first. Do not substitute arbitrary versions.

## Windows

Use Visual Studio 2022 Desktop development with C++ (C11-capable MSVC), CMake
3.24+ and Ninja from an x64 Native Tools prompt. The same CMake source fetches
build SDL locally; no Linux-specific toolchain is needed. PowerShell:

```powershell
cmake -S LVGL -B LVGL/build-win -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build LVGL/build-win --parallel 4
.\LVGL\build-win\bin\turnhub_sim.exe --device sigil --zoom 2
ctest --test-dir LVGL/build-win --output-on-failure
```

MSVC builds LVGL as a DLL because 9.3's global-data exports require it; CMake
places the DLL next to the executable. Keep both when sharing a build. SDL is
static in the default source route. Windows/MSVC instructions are **not yet
validated in this Linux workspace**. WSL2 + WSLg can also use the Linux commands;
without WSLg use the headless tests. Do not change legacy PlatformIO to build this.

## Interaction

| Input | Gameplay | Menus |
|---|---|---|
| Mouse click | Simulated capacitive touch; life +/- on Sigil, Atlas cards/settings | Activate button on release |
| Left / Right | Request selected player's life −1 / +1 | Previous / next focused control |
| Up | Settings/menu | Previous focused control |
| Down | Cycle local selected player | Next focused control |
| Enter | Advance deterministic sample (demo-only substitute for future contextual action) | Activate focused control |
| M / Escape | Open settings / return to gameplay | Same shortcuts |
| T | Cycle Graphite → Daylight → Brass → High contrast | Same, keeping current screen |
| N | Next deterministic sample | Same |
| Space | Request pause/resume in fixture provider | Same |

The Sigil life buttons and keyboard actions reach the same action sink. On Atlas,
card taps open local player selection; full production player-detail controls are
future work. Battery is explicitly `BAT --`, not a fabricated percent. Pairing is
labeled **MOCK / no radio**, with code comparison, mock pair and unpair requests;
there are no real pairing credentials or resets. Themes are runtime-only; no NVS
writes. Gameplay state is represented with words as well as color.

Four deterministic samples: running/Rowan turn (40,27,13,0 life), running/Mira
turn, paused, game over/Mira winner. Vale is inactive. Running countdown updates
once per second; zero does not automatically pass or finish a game. Sample life
edits are bounded visual fixture edits; there are no rules, statistics, profile
persistence or transport in `Fixtures`. Pausing freezes its countdown.

```sh
LVGL/build/bin/turnhub_sim --device atlas --theme brass --scenario 2
LVGL/build/bin/turnhub_sim --device sigil --screen pairing
LVGL/build/bin/turnhub_sim --help
```

## Checks and captures

```sh
ctest --test-dir LVGL/build --output-on-failure
python3 LVGL/tools/generate_fonts.py --check
python3 design/build_tokens.py --check
python3 design/build_avatar_masks.py --check
python3 Atlas/tests/host/audit_adapters.py
```

CTest runs each profile with SDL's dummy video/audio drivers. `--self-test`
injects **SDL pointer press/release and keyboard events** through actual LVGL
input devices, exercising resolution, focus/select, life action routing, player
selection, all themes, menu/pair mock, running/paused/game-over samples and timer.
Checks throw on failure even in Release builds. They are UI/fixture tests, not
hardware or game-engine acceptance. Capture validation separately launches the
SDL X11 backend on a virtual display when a physical desktop is unavailable:

```sh
mkdir -p LVGL/previews
LVGL/build/bin/turnhub_sim --device atlas --capture LVGL/previews/atlas.bmp
LVGL/build/bin/turnhub_sim --device sigil --capture LVGL/previews/sigil.bmp
# Headless X11 example, requires xvfb + xauth:
xvfb-run -a LVGL/build/bin/turnhub_sim --device sigil --capture LVGL/previews/sigil.bmp
# Pure headless launch (not evidence of a visible window):
SDL_VIDEODRIVER=dummy LVGL/build/bin/turnhub_sim --device atlas --frames 40
```

A standard-library capture matrix validates all four themes on both profiles,
plus pairing, paused and game-over scenes (14 captures):

```sh
python3 LVGL/tests/capture_matrix.py --executable LVGL/build/bin/turnhub_sim \
  --output LVGL/previews/matrix --driver x11
```

It needs a working `DISPLAY` for X11; `--driver dummy` validates software rendering
without a window. It checks exact dimensions, token background colors, nonblank
pixels and distinct outputs and writes PNGs plus SHA256 metadata. It is a property
check, not a golden-image or OCR accessibility test.

Optional GCC/Clang lifetime checks (development only):

```sh
cmake -S LVGL -B LVGL/build-asan -G Ninja -DCMAKE_BUILD_TYPE=Debug -DTURNHUB_SANITIZERS=ON
cmake --build LVGL/build-asan --parallel 4
ctest --test-dir LVGL/build-asan --output-on-failure
```

BMP output captures the actual SDL renderer. `--capture` exits after rendering;
`--frames N` is a bounded launch check. `--zoom` changes window pixels, not logical
profile dimensions; use zoom 1 for exact-resolution captures. `previews/` and
build products are ignored; selected review PNGs can be retained in engineering
history. The launcher does not require an image conversion package.

## Source boundaries and assets

| File | Responsibility |
|---|---|
| `include/presentation.hpp` | Internal snapshot, action sink, display profile, semantic five-way events; no GPIO/LVGL/SDL/game rules |
| `src/fixtures.cpp` | Deterministic synthetic provider, fixture-only edits and clock |
| `src/ui.cpp` | Shared widgets, profile density, local screen/theme/selection, action requests |
| `src/sdl_input.cpp` | Keyboard adapter; queues events, LVGL calls stay on main thread |
| `src/main.cpp` | SDL display/pointer bootstrap, event loop, CLI, integration self-tests, capture |
| `src/assets.cpp` | Existing TurnHub 64px alpha presets via replaceable asset provider |
| `assets/th_*.c` | Generated Inter/Cinzel glyphs, tabular digit advances, checked source/output digests |
| `lv_conf.h` | Desktop LVGL configuration only; software rendering and development assertions |

`Ui` consumes `SnapshotSource` and emits `ActionSink::request`. The fixture object
implements those interfaces **only in the desktop executable**. Production adapters
must map to existing authoritative Intent/event paths; never copy fixture behavior
into firmware. Widgets accept a hardware-neutral `NavEvent`; a future PCA9554 driver
can use it without SDL or register knowledge in the widgets. Four player cards are
the first fixture layout; pagination/overflow for all eight seats remains future UI.

Assets use the existing generated 64px alpha masks; custom JPEG decoding and
artwork backgrounds are not implemented. Existing Atlas upload/approval and SD
storage remain the only connected profile/artwork store. See migration doc for
512px decode memory, derivative/cache and shared-card-lock considerations.

Regenerate fonts only when changing source/specification:

```sh
npm ci --prefix LVGL/tools --ignore-scripts --no-audit --no-fund
python3 LVGL/tools/generate_fonts.py
python3 LVGL/tools/generate_fonts.py --check
```

Node 18+ and Python 3.9+ suffice; converter **lv_font_conv 1.5.3** is pinned by
package lock (bundled tool dependencies stay inside ignored node_modules).
Normal fonts use Inter; Brass headings use Cinzel, with existing gold/walnut
palette and outlined panels. Latin/ASCII initial glyph coverage is deliberate;
non-Latin names need a future glyph/fallback strategy.

## Implementation status and remaining work

Both profiles, shared themes, gameplay screens, deterministic samples, running
clock, pointer/five-way input, mock pairing/unpairing, settings and player selection
are implemented. SD JPEG decoding, artwork backgrounds, eight-seat pagination,
production setup/settings, real recovery/reset, embedded rendering, network/Intent
bindings, physical drivers, battery detection, power control and S3 OTA are pending.
No production renderer is replaced. Follow hardware verification before GPIO bindings.
