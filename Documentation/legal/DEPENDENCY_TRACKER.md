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
| LVGL | 9.3.0, release commit `c033a98afddd65aaafeebea625382a94020fe4a7`; hash-pinned CMake archive | Isolated desktop UI foundation | MIT | Desktop binary only; no legacy firmware integration | GREEN | Preserve upstream notice in `LVGL/licenses/LVGL-MIT.txt`; audit any enabled bundled components before release. |
| LVGL built-in fallback glyphs | Montserrat 14 + Font Awesome 5 glyphs bundled in LVGL 9.3.0; linked default font verified with `nm` | Internal LVGL default; visible UI explicitly uses TurnHub Inter/Cinzel | SIL OFL 1.1 for font glyphs | Yes, desktop binary | GREEN | Preserve `LVGL/licenses/OFL-Montserrat.txt` and `FontAwesome-Free.txt`. No upstream SVG/JS icons are copied. |
| SDL | 2.32.4, hash-pinned official source archive | LVGL desktop display/input | zlib | Desktop binary only | GREEN | Preserve `LVGL/licenses/SDL-zlib.txt`; default static source build, optional exact-version system library. |
| lv_font_conv | 1.5.3, npm lockfile and upstream bundled dependencies | Development font conversion only | MIT; bundled dependencies retain their own MIT/BSD notices | Tool is not shipped; generated font bitmaps are OFL | GREEN | Preserve tool license in `LVGL/licenses`; retain bundled notices for any distributed tool copy; Inter/Cinzel OFL already recorded. |
| Arduino-ESP32 | PlatformIO `espressif32`, Arduino framework | Atlas + Sigil | LGPL-2.1 | Yes, firmware build | YELLOW | Confirm exact release version/components and satisfy LGPL distribution/relinking/source obligations for the chosen firmware distribution model. Preserve notices. |
| Inter and Cinzel variable fonts (V1 design system) | Inter 4 `Inter[opsz,wght].ttf` and Cinzel `Cinzel[wght].ttf` from [google/fonts](https://github.com/google/fonts) (`ofl/inter`, `ofl/cinzel`), fetched 2026-10-02, subset to Latin with fontTools; committed as `design/fonts/*.ttf` and `*.woff2` (see `design/README.md`) | Android app and the LVGL simulator | SIL Open Font License 1.1 (`design/fonts/OFL-Inter.txt`, `design/fonts/OFL-Cinzel.txt`) | Yes, APK | GREEN | Ship the OFL texts with app open-source notices and firmware notices. The admin portal uses system fonts. Subsetting creates a Modified Version; neither font declares a Reserved Font Name, so the family names may stay. Never sell the fonts by themselves. |
| Adafruit NeoPixel | `adafruit/Adafruit NeoPixel@1.15.5`, [upstream](https://github.com/adafruit/Adafruit_NeoPixel) | Sigil (`sigil` env) status ring only | LGPL-3.0 ([license](https://github.com/adafruit/Adafruit_NeoPixel/blob/master/COPYING)) | Yes if Sigil firmware ships | YELLOW | Statically linked, so LGPL-3.0 requires letting recipients relink against a modified library (e.g. provide object files or the full source) and preserving notices. Covered by the same pre-release review as GxEPD2 (GPL-3.0) in the same firmware. |
| Mbed TLS | 2.28.7, bundled inside Arduino-ESP32 (framework-arduinoespressif32 2.0.17); no separate PlatformIO dependency | Atlas + Sigil secure link (X25519, AES-128-CCM, HMAC-SHA256; `shared/include/secure_link_mbedtls.h`, 2026-09-28) | Apache-2.0 (upstream also offers GPL-2.0-or-later; Espressif ships it under Apache-2.0) | Yes, linked into both firmwares | GREEN | Preserve the Apache-2.0 license and NOTICE in release materials with the other Arduino-ESP32 notices. TurnHub calls its primitives only; no mbedTLS source is copied into the repository. The RFC test vectors in `secure_link.h` are published values from RFC 7748, 4231 and 3610. |
| PlatformIO Core | Build environment | Development | Apache-2.0 | No, currently build tooling only | GREEN | Track tool version for reproducibility. No product notice expected unless redistributed. |
| Python `cryptography` | 46.0.7 (pip in CI; locally the copy bundled with PlatformIO's esptool) | `tools/firmware/thfw.py`: signs and checks OTA packages (2026-09-29) | Apache-2.0 or BSD-3-Clause | No, developer and CI tooling only | GREEN | Not shipped. The signatures it makes are verified on the devices by Mbed TLS and in the app by `java.security`. |
| GitHub Actions: checkout, setup-python, setup-java, upload-artifact | Official `actions/*` repositories; exact commit revisions in `.github/workflows/ci.yml` | Development CI only | MIT (upstream `LICENSE` in each action repository) | No, not embedded in firmware or APK | GREEN | Preserve upstream notices if action code is ever redistributed. Workflow invokes the actions without copying their implementation into TurnHub. |
| GitHub Action: r0adkll/upload-google-play | `r0adkll/upload-google-play` v1.1.5, pinned to commit `e738b9dd8f2476ea806d921b64aacd24f34515a5` in `.github/workflows/ci.yml` | Development CI only: publishes master's Android bundle to Play internal testing | MIT (upstream `LICENSE`) | No, not embedded in firmware or APK | GREEN | Receives the Play service account key at run time; keep it pinned to a reviewed commit. |
| org.json (JSON-java) | `org.json:json:20260814`, Android `testImplementation` only | Android JVM unit tests (the app itself uses Android's built-in `org.json`) | Public Domain (per its Maven POM) | No, test classpath only | GREEN | None while test-only. Re-review if it ever moves to an `implementation` dependency. |
| AndroidX CameraX (`camera-camera2`, `camera-lifecycle`, `camera-view`) | `androidx.camera:*:1.5.0` | Android app: Admin-code QR scanner preview | Apache-2.0 | Yes, Android APK | GREEN | Include the Apache-2.0 license and any NOTICE text in the app's open-source notices. Camera use is optional and asked for only when the player taps Scan. |
| ZXing core | `com.google.zxing:core:3.5.3`, [upstream](https://github.com/zxing/zxing) | Android app: on-device QR decoding (no network) | Apache-2.0 | Yes, Android APK | GREEN | Include the Apache-2.0 license and NOTICE in the app's open-source notices. |

## Platform/toolchain follow-up

iOS foundation: Swift 6, SwiftPM and XCTest are development tools; Foundation
and SwiftUI are Apple platform frameworks used under the applicable SDK terms.
There is no third-party iOS runtime package or project generator. Simulator CI
uses the official checkout action already tracked above. Before distribution,
review Apple SDK/developer agreement obligations and record any fonts, artwork
or additional packages that actually enter the app. Build requirements are in
[iOS/README.md](../../iOS/README.md).

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
| Web UI fonts | Admin portal: system fonts; Android app: Inter and Cinzel, recorded under software dependencies | GREEN | Keep the OFL texts with app notices; the portal embeds no web fonts. |
| Web UI icons/images | No external icon/image package identified in current ESP32 portal | GREEN | Do not paste icons/art from another product. Record any future asset pack before use. |
| V1 icon set and brand mark | Original line icons drawn for TurnHub on 2026-10-02 (`design/icons/*.svg`) and an original turn-arrow mark; no third-party icon set | GREEN | Keep new icons original; record the source of any icon that is not. |
| Sigil display graphics | Programmatic geometry (gears, rivets, gauges) plus the Cinzel/Oswald bitmap fonts recorded above; no third-party art identified | GREEN | Continue using original/programmatic assets unless a licensed source is recorded. |
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

- [ ] Resolve GxEPD2 GPLv3 strategy before any commercial distribution of Gen 1 Sigil firmware (`legacy/gen1` only; master no longer links GxEPD2).
- [ ] Correct the placeholder `[year] [fullname]` in the root `LICENSE.txt` and confirm the intended license scope.
- [ ] Produce an exact production SBOM/dependency manifest from Atlas and Sigil release builds.
- [ ] Add complete required upstream license texts/notices to the release package.
- [ ] Review trademark compatibility wording before Kickstarter, retail packaging, app-store publication, or paid promotion.
- [ ] Add Android dependencies here as soon as the native project is generated.
- [ ] Perform a final third-party asset audit before public release.

## Change log

### 2026-10-13 (Gen 1 split)

GxEPD2, Adafruit GFX, Adafruit SH110X, Adafruit BusIO and the vendored Wokwi
e-paper chip left master with the Gen 1 Sigil displays and Wokwi build, and
LovyanGFX and the Cinzel/Oswald (and Inter) device bitmap fonts with the Gen 1
Atlas TFT; their rows remain in this file on the `legacy/gen1` branch, which
still ships them.

### 2026-09-28 (GitHub Actions)

Recorded the four official GitHub actions used for checkout, language setup and
development artifact retention. They run on GitHub-hosted build machines and do
not add a device/runtime dependency.

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

### Artwork build tools (2026-10-10)

CairoSVG 2.8.2 (LGPL-3.0-or-later) and Pillow 11.3.0 (HPND/Pillow license)
are development-only rasterization tools pinned in `tools/artwork-requirements.txt`.
They generate alpha masks from TurnHub's original SVG artwork and are not linked
or bundled into Atlas or Android. CI provenance checking uses stdlib only.
No third-party artwork or new production image/crop library is introduced.
