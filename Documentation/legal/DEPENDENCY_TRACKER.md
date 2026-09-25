# TurnHub Dependency and IP Tracker

This is the working tracker for third-party software, assets, trademarks, externally sourced design material, and other IP that may create attribution, licensing, or release obligations.

The goal is simple: know what entered the project, where it came from, how it is used, and what must happen before distribution.

## Status legend

- **GREEN** - permissive or tool-only dependency with straightforward obligations.
- **YELLOW** - compatible in principle, but distribution details or exact obligations still need confirmation.
- **RED** - strong copyleft, unknown license, noncommercial restriction, incompatible terms, or another issue that must be resolved before production distribution.
- **N/A** - reference/trademark/design item rather than a software license.

## Current software dependencies

| Item | Version/source currently known | Used by | License | Ships with product? | Status | Required action |
| --- | --- | --- | --- | --- | --- | --- |
| Arduino-ESP32 | PlatformIO `espressif32`, Arduino framework | Atlas + Sigil | LGPL-2.1 | Yes, firmware build | YELLOW | Confirm exact release version/components and satisfy LGPL distribution/relinking/source obligations for the chosen firmware distribution model. Preserve notices. |
| GxEPD2 | `zinggjm/GxEPD2`, current PlatformIO dependency | Sigil e-ink | GPL-3.0 | Yes if current Sigil firmware ships | RED | Decide before production whether to comply with GPLv3 for the resulting firmware or replace with a suitably licensed display driver. Legal review before commercial release. |
| Adafruit GFX Library | Transitive GxEPD2 / SH110X dependency | Sigil e-ink and OLED graphics | BSD-style | Yes if linked into shipped firmware | GREEN | Preserve copyright, conditions, and disclaimer in release notices/materials as required. |
| LovyanGFX | `lovyan03/LovyanGFX@1.2.30` (PlatformIO) | Atlas TFT (E32R28T) | FreeBSD (BSD-2-Clause) for LovyanGFX. Bundled Adafruit (BSD) and Bodmer (FreeBSD) code carries its own notices | Yes, Atlas firmware | GREEN | Preserve the combined notices in `license.txt` in release materials. Atlas uses only the DejaVu fonts (Bitstream Vera-style license) and the glcd font; do not use the bundled GNU FreeFont `Free*` fonts (GPL with font exception) without review. |
| Adafruit SH110X | `adafruit/Adafruit SH110X@2.1.15`, [upstream](https://github.com/adafruit/Adafruit_SH110x) | Optional Sigil OLED environment only | BSD-3-Clause ([license](https://github.com/adafruit/Adafruit_SH110x/blob/master/license.txt)) | Yes if OLED firmware ships | GREEN | Preserve upstream copyright, license, disclaimer and required source notices in release materials; the vendor splash is disabled in the build. |
| Adafruit NeoPixel | `adafruit/Adafruit NeoPixel@1.15.5`, [upstream](https://github.com/adafruit/Adafruit_NeoPixel) | Sigil E-ink build (`sigil` env) status ring only | LGPL-3.0 ([license](https://github.com/adafruit/Adafruit_NeoPixel/blob/master/COPYING)) | Yes if E-ink Sigil firmware ships | YELLOW | Statically linked, so LGPL-3.0 requires letting recipients relink against a modified library (e.g. provide object files or the full source) and preserving notices. Covered by the same pre-release review as GxEPD2 (GPL-3.0) in the same firmware. |
| Adafruit BusIO | Transitive GFX / SH110X dependency | Sigil graphics bus support | MIT ([license](https://github.com/adafruit/Adafruit_BusIO/blob/master/LICENSE)) | Yes if linked into shipped firmware | GREEN | Preserve copyright and license notice. |
| PlatformIO Core | Build environment | Development | Apache-2.0 | No, currently build tooling only | GREEN | Track tool version for reproducibility. No product notice expected unless redistributed. |
| wokwi-ws29v2-custom-chip | `bonnyr/wokwi-ws29v2-custom-chip` release v0.0.5 `chip.zip` (`chip.wasm` SHA-256 `6f59d1873e3faa07a018a088f61b1ffff6a8bcfebf7925eadd7e2082dd318d9d`), vendored as `Sigil/wokwi/chips/epaper-2in13.chip.wasm` with a TurnHub `chip.json` (2.13" geometry) | Sigil Wokwi simulation only | MIT (copy in `Sigil/wokwi/chips/LICENSE-wokwi-ws29v2-custom-chip.txt`) | No, development simulation only | GREEN | Keep the license file with the binary. Not linked into firmware. |
| org.json (JSON-java) | `org.json:json:20260814`, Android `testImplementation` only | Android JVM unit tests (the app itself uses Android's built-in `org.json`) | Public Domain (per its Maven POM) | No, test classpath only | GREEN | None while test-only. Re-review if it ever moves to an `implementation` dependency. |

## Platform/toolchain follow-up

Optional Windows native-test compiler: PlatformIO package
`platformio/toolchain-gccmingw32` 1.50100.0 (GCC 5.1.0), used by
`Atlas/tests/host/run-gcc.ps1`. Installed package provenance is recorded in its
`package.json`; component license texts are in its `licenses/` directory.
This is development-only tooling, not linked into ESP32 firmware. Compiler and
native test binaries are not committed or distributed. Review the package's
component/runtime redistribution terms before distributing either; MSVC remains
an alternative via `run.cmd`.

The high-level rows above are not yet a complete software bill of materials. Before release, generate the dependency list from the exact production builds and inventory:

- Espressif SDK/framework components pulled into Atlas firmware.
- Espressif SDK/framework components pulled into Sigil firmware.
- Any binary blobs, bootloaders, partition tools, OTA components, crypto libraries, or other linked code included by the toolchain.
- Android application dependencies once the native client is created.
- Any future BLE, QR, image, persistence, networking, or serialization libraries.

## Assets and externally sourced material

| Item | Current state | Status | Rule |
| --- | --- | --- | --- |
| Web UI fonts | System font stack only in current ESP32 portal; no bundled web font identified | GREEN | If a font file is ever bundled, record font name, source, license, and redistribution terms. |
| Web UI icons/images | No external icon/image package identified in current ESP32 portal | GREEN | Do not paste icons/art from another product. Record any future asset pack before use. |
| Sigil display graphics | Current code uses programmatic text/geometry; no third-party art identified | GREEN | Continue using original/programmatic assets unless a licensed source is recorded. |
| Sounds/tones | Programmatic buzzer tones; no sampled third-party audio identified | GREEN | Record any future audio file, sound pack, or melody source and license. |
| CAD/enclosure files | No external production enclosure asset recorded in this tracker | GREEN | If a downloaded 3D model is used or modified, record source and model license before committing/shipping it. |
| Schematics/reference designs | No third-party schematic copied into the production tree is currently recorded | GREEN | Record vendor/reference-design source and applicable terms if incorporated. |

## Trademark and compatibility references

| Reference | Current use | Status | Required action |
| --- | --- | --- | --- |
| Magic: The Gathering / MTG | Game profile/compatibility wording in documentation and legacy UI | N/A | Keep use descriptive. Avoid logos, card art, mana symbols, card frames, or implication of affiliation. Review commercial wording before launch. |
| Commander / MTG Commander | Game profile and commander-damage terminology | N/A | Same as above. Review exact naming/disclaimer strategy before public commercial release. |
| Yu-Gi-Oh! | Game profile/compatibility wording | N/A | Keep descriptive and avoid branded visual assets or implied endorsement. Review before launch. |

## Open IP/legal tasks

- [ ] Resolve GxEPD2 GPLv3 strategy before any commercial Sigil firmware distribution.
- [ ] Correct the placeholder `[year] [fullname]` in the root `LICENSE.txt` and confirm the intended license scope.
- [ ] Produce an exact production SBOM/dependency manifest from Atlas and Sigil release builds.
- [ ] Add complete required upstream license texts/notices to the release package.
- [ ] Review trademark compatibility wording before Kickstarter, retail packaging, app-store publication, or paid promotion.
- [ ] Add Android dependencies here as soon as the native project is generated.
- [ ] Perform a final third-party asset audit before public release.

## Change log

### 2026-09-25 (Sigil status ring)

Added Adafruit NeoPixel (LGPL-3.0) for the E-ink Sigil's Jewel 7 status ring.
No third-party code was copied into the tree.

### 2026-09-24 (Sigil OLED scaffold)

Added the optional SH110X driver, recorded BusIO and extended the GFX usage
record. No third-party implementation code or photo was copied into the tree.

### 2026-09-24 (Atlas display)

Added LovyanGFX for the Atlas E32R28T TFT, and recorded the font restriction.

### 2026-09-24

Added the test-only `org.json:json` dependency introduced with the Android live-Atlas integration. The broader Android dependency inventory (AndroidX, Compose, coroutines, JUnit) remains the open task above.

### 2026-09-19

Initial tracker created from the current `intent-android-foundation` branch. Known direct dependencies and current game-name compatibility references were inventoried. GxEPD2 was marked RED for production review because the current Sigil firmware directly uses it under GPLv3.
