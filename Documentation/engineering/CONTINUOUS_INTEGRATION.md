# GitHub Actions checks

The workflow is [`.github/workflows/ci.yml`](../../.github/workflows/ci.yml).
GitHub runs it for pull requests targeting `master` and pushes to `master`.
After the workflow reaches `master`, it can also be started from **Actions >
TurnHub CI > Run workflow**. A new push cancels an older run for the same PR or
branch. A PR run tests GitHub's proposed merge with the base branch.

## Checks

| Check name | Work performed |
| --- | --- |
| Host tests and contracts | Atlas gameplay, storage and profile-store suites; Sigil OLED, LED, menu, secure-link and firmware-package suites; adapter audit; generated-response and shared-fixture contract validation; the `tools/firmware/thfw.py` packaging tests |
| Firmware (atlas) | Build Atlas with PlatformIO; check its firmware descriptor and, with the signing secret, add a signed `atlas.thfw` OTA package |
| Firmware (sigil) | Build the e-ink/joystick Sigil; descriptor check and signed `sigil-eink.thfw` as for Atlas |
| Firmware (sigil-oled) | Build the OLED/button Sigil; descriptor check and signed `sigil-oled.thfw` as for Atlas |
| Firmware (sigil-wokwi) | Compile the Wokwi variant; does not execute the simulator |
| Android build and unit tests | Build the debug APK and run JVM unit tests |

The hardware `TestHarness/` target was retired on 2026-10-05. CI no longer
builds it or publishes harness firmware artifacts. Its source is historical and
unsupported; the Atlas and Sigil host suites remain supported.

Host suites use GCC, C++14, AddressSanitizer and UBSan on Ubuntu 24.04, with
assertions enabled. Sanitizer failures fail the check. The firmware jobs use
PlatformIO Core 6.2.0 and the existing project configurations. Some platform and
library versions are still floating in those configurations; this workflow does
not claim reproducible release builds. Each firmware artifact records its commit,
environment and resolved package versions.

Android uses the committed Gradle wrapper and Temurin 26, matching
`Android/gradle/gradle-daemon-jvm.properties`. The hosted runner supplies the Android
SDK; after license acceptance, Gradle installs the SDK components requested by
the project. Keep the workflow's Java version aligned with the daemon criteria.

The workflow uses read-only repository permission and pinned revisions of
official GitHub actions. Its one secret is `TURNHUB_FIRMWARE_SIGNING_KEY`, the
firmware signing key ([Sigil OTA](SIGIL_OTA.md#keys-and-signing)); without it
(for example on a fork's pull request) builds are left unsigned and still
pass. It does not flash devices, publish a release, or modify repository
content/settings.

Releases are a separate workflow,
[`.github/workflows/release.yml`](../../.github/workflows/release.yml): pushing
a tag like `v0.9.0` builds Atlas and both Sigils, signs them, and publishes a
GitHub Release with the three `.thfw` packages and `turnhub-firmware.json`, the
feed the Android app reads. It needs `contents: write` and fails without the
signing secret. Branch protection is configured
separately in GitHub, not by this file.

## Reading results

1. Open the PR and expand its checks, or open the repository's **Actions** tab.
2. A successful check means its commands passed for that run's commit. A failed
   check links to the job and failing step. Open the log before retrying; a source
   defect needs a fix, while a download/service failure may only need a rerun.
3. Open a successful run's **Artifacts** to download per-target firmware or the
   Android debug APK. Android test reports are retained when available, including
   on failed test runs. Artifacts expire after seven days.

Firmware artifacts contain `firmware.bin`, `firmware.elf`, and `build-info.txt`.
Atlas artifacts also retain `firmware.map` and `stack-usage.tar.gz` (compiler
`.su` reports) for decoding panics and comparing handler working sets. Keep
the artifact from the exact build tested on hardware.
They are development app images, not a complete factory-flash bundle. Match the
environment to the device and retain the normal PlatformIO upload process for
initial provisioning. PR artifact names identify the tested merge commit, which
may differ from the branch head or the eventual merge commit.

The `atlas` and `sigil` firmware jobs also upload **screen previews**
(`screens-atlas-<sha>`, `screens-sigil-<sha>`, kept 14 days, added
2026-09-29): PNGs of the Atlas touchscreen, e-ink Sigil and OLED Sigil screens,
rendered on the runner from the real display code
(`Atlas/tests/host/render-atlas-screens.sh`,
`Sigil/tests/host/render-sigil-screens.sh`). The Atlas render fails the job if
a region-by-region redraw differs from a full redraw. Previews show the design,
not the panel: colors, contrast and ghosting still need the bench.

CI artifacts are not accepted releases; hardware behavior is checked on the
bench. Browser smoke tests and Android device/UI tests are not part of CI.

## Local equivalents

From the repository root on Linux with GCC and Python 3 installed:

```sh
bash Atlas/tests/host/run-linux.sh
bash Sigil/tests/host/run-linux.sh
python3 Atlas/tests/host/audit_adapters.py
python3 Atlas/tests/host/check_client_contract.py
python3 Android/tools/export_manual.py --check
```

The Linux runners build and run the existing suites; they do not introduce new
gameplay tests. They stop at the first failing command. `CXX` can name an alternate
compatible compiler executable. When adding/removing a host-linked source, update
the Linux and Windows runner source lists together. The Windows runners remain
available for the development PC.

In a restricted local container where LeakSanitizer cannot inspect processes,
`ASAN_OPTIONS=detect_leaks=0` can be set explicitly for a local run. Record that
limitation with the result; CI does not disable leak detection.

## Making checks required later

First establish a successful run, then configure a rule for `master` under the
repository's **Settings > Rules > Rulesets** (or branch protection). Require a PR
and the six check names above, using GitHub Actions as their source. Keep check
names stable when editing the workflow. Do not add path filters that prevent a
required check from reporting. A solo-maintainer setup does not need a mandatory
second person's approval just to require checks.

This workflow does not create that rule or merge any existing PR. It can be
reviewed and merged independently of the prototype stabilization work in PR #17.
