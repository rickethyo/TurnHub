# TurnHub Third-Party Notices

TurnHub uses third-party open-source software and development tools. This file is the human-readable notice index for components currently known to the project.

This is a working development record, not a substitute for the complete license texts or a release-time legal review. Before any commercial hardware or software distribution, the dependency tracker in `Documentation/legal/DEPENDENCY_TRACKER.md` must be reviewed and the required upstream notices/license texts must be included with the distributed product.

## Current third-party software

| Component | Copyright / project owner | Use in TurnHub | Upstream license | Distribution note |
| --- | --- | --- | --- | --- |
| LVGL 9.3.0 | LVGL Kft and contributors | Isolated desktop UI simulator | MIT | Preserve [full upstream license](LVGL/licenses/LVGL-MIT.txt) with desktop distribution; no legacy firmware link. |
| LVGL default font glyphs | Montserrat and Font Awesome projects | Bundled internal fallback in LVGL 9.3; visible UI uses Inter/Cinzel | SIL OFL 1.1 for font glyphs | Preserve [Montserrat OFL](LVGL/licenses/OFL-Montserrat.txt) and [Font Awesome font notice](LVGL/licenses/FontAwesome-Free.txt). |
| SDL 2.32.4 | SDL project and contributors | Desktop display and input | zlib | Preserve [upstream license](LVGL/licenses/SDL-zlib.txt) and source notices. |
| lv_font_conv 1.5.3 | LVGL font converter project | Development-only generation of Inter/Cinzel bitmaps | MIT | [Tool license](LVGL/licenses/lv-font-conv-MIT.txt); font output remains under the existing [Inter](design/fonts/OFL-Inter.txt) and [Cinzel](design/fonts/OFL-Cinzel.txt) OFL notices. |
| Arduino-ESP32 | Espressif Systems and contributors | Arduino framework used by ESP32 Atlas and Sigil firmware | LGPL-2.1 | Firmware distribution obligations must be verified before release. Preserve upstream notices and license terms. |
| PlatformIO Core | PlatformIO Labs and contributors | Development/build tooling | Apache-2.0 | Development tool, not presently intended to be embedded in or shipped as part of TurnHub firmware. Track for build provenance. |

## Framework and platform components

ESP32 firmware builds may contain or depend on additional Espressif framework components, SDK code, libraries, and binary components beyond the high-level Arduino-ESP32 entry above. Those components must be inventoried from the exact release build before product distribution. A toolchain dependency being installed automatically does not remove its license obligations.

## Third-party trademarks and compatibility references

TurnHub documentation and prototype software may refer to third-party game names, including:

- Magic: The Gathering
- Commander / MTG Commander
- Yu-Gi-Oh!

These names are used as compatibility or game-profile references. TurnHub does not currently include their logos, card artwork, card frames, mana symbols, or other branded visual assets in the repository.

Before a commercial release, compatibility wording, attribution/disclaimer language, product packaging, app-store copy, and marketing materials should receive trademark review. TurnHub should not imply sponsorship, endorsement, or affiliation unless one actually exists.

Death May Die condition/relic names are descriptive game references. Card-reference
photographs are © CMON, hosted by BoardGameGeek, downloaded only when opened and
cached on the device. They are not bundled or covered by TurnHub's license.
Sources and release review are recorded in the
[dependency tracker](Documentation/legal/DEPENDENCY_TRACKER.md).

## TurnHub-owned material

TurnHub source code, protocol documents, product names, hardware concepts, UI code, and other original project material are separate from the third-party components listed above. The repository's root license does not relicense third-party code under TurnHub's chosen license.

The current root `LICENSE.txt` still contains placeholder copyright fields and must be corrected before any formal release.

## Maintenance rule

When adding a library, framework, font, icon set, image, sound, copied code snippet, CAD model, schematic source, SDK, sample implementation, or other externally sourced material:

1. Record it in `Documentation/legal/DEPENDENCY_TRACKER.md` before or with the change.
2. Record the source/owner and exact license or permission.
3. Record whether it is merely a development tool or is shipped/linked/embedded with TurnHub.
4. Preserve required notices and attribution.
5. If the license is unknown, custom, copyleft, noncommercial, source-available, or otherwise restrictive, do not treat it as approved for production until reviewed.

Last reviewed: 2026-09-19
