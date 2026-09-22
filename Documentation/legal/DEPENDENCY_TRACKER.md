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
| Adafruit GFX Library | Transitive GxEPD2 dependency | Sigil e-ink graphics | BSD-style | Yes if linked into shipped firmware | GREEN | Preserve copyright, conditions, and disclaimer in release notices/materials as required. |
| pySerial | `pyserial>=3.5` | Legacy Python controller/tooling | BSD-3-Clause | Only if legacy tooling is distributed | GREEN | Preserve notice/disclaimer if distributed. Remove from production manifest if no longer shipped. |
| PlatformIO Core | Build environment | Development | Apache-2.0 | No, currently build tooling only | GREEN | Track tool version for reproducibility. No product notice expected unless redistributed. |

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

### 2026-09-19

Initial tracker created from the current `intent-android-foundation` branch. Known direct dependencies and current game-name compatibility references were inventoried. GxEPD2 was marked RED for production review because the current Sigil firmware directly uses it under GPLv3.
