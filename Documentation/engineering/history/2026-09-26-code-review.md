# Code review, 2026-09-26 (all code except Android)

Scope: Atlas, Sigil, `shared/`, TestHarness, the host tests and the
KiCad tools. Method: every host suite rebuilt with GCC 13 on Linux with
`-Wall -Wextra` plus AddressSanitizer and UBSan, the adapter audit and client
contract check, then targeted reading of the untested paths (radio receive,
packet validation, string/path building). No firmware build or hardware
run was done here; PlatformIO was not installed.

## Results

| Check | Result |
|---|---|
| Atlas `scenarios` (ASan+UBSan) | Pass, after the test fix below |
| Atlas `storage_scenarios`, `profile_store_scenarios` | Pass, clean |
| Sigil `oled_scenarios`, `led_scenarios`, `menu_scenarios` | Pass, clean |
| `audit_adapters.py` | Pass (32 adapters) |
| `check_client_contract.py` | Pass (10 responses, 9 fixtures) |

## Fixed (branch `claude/code-review-1gfzqr`)

1. **Host test use-after-scope** (`Atlas/tests/host/scenarios.cpp`): `pressButton`
   and the pair-slop scenario kept a `TouchButton*` into a temporary `AtlasScreen`.
   It passed by luck on MinGW; AddressSanitizer aborts. Test-only.
2. **Clipped fallback text on the Atlas code screen** (`touch_controls.cpp`): with
   no profile name, "a signed-in phone" went through the 13-byte screen-name buffer
   and showed as "For a signed-in  (NN s)". Now only real names are clipped.
3. **`AdjustLife` missing from the menu-select switch** (`sigil_input.cpp`): never
   selected (it frees Left/Right for LifeAdjust packets), so no behavior change; the
   explicit case documents it and clears `-Wswitch`.

## Findings not changed

- **Host tests skip several application modules without saying so.** Besides the
  four hardware files named in `CLAUDE.md`, the runners also leave out
  `profile_stats_bridge.cpp`, `sigil_bus.cpp`, `ota_manager.cpp`,
  `game_settings_store.cpp`, `web_pages.cpp` and `factory_reset.cpp` (the last is
  stubbed in `test_globals.cpp`). Fixed for the bridge: it is now linked into
  `scenarios`, and the test's game-completed hook calls the firmware's own
  `TurnHubProfileStats::persistCompletedGame` (new `profile_stats_bridge.h`), so
  the "statistics once" scenarios run through the real bridge. The others remain
  firmware-only.
- **Two remaining GCC truncation warnings are false positives**
  (`touch_controls.cpp` presence code, always 6 digits; `web_profile_api.cpp`
  colour, 24-bit), and the `strcpy`/`strcat` in `sd_blob_store.cpp` are length
  checked first.
- **Radio receive paths look sound.** Atlas, Sigil and the harness copy packets
  into FreeRTOS queues from the ESP-NOW callback; the Sigil and harness accept only
  exact known packet sizes before `memcpy`.
