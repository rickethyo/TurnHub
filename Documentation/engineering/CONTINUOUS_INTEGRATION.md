# Continuous Integration

The workflow is [`.github/workflows/ci.yml`](../../.github/workflows/ci.yml).
GitHub runs it for pull requests targeting `master` (only the parts the PR
touches; see "What runs") and pushes to `master` (everything).
After the workflow reaches `master`, it can also be started from **Actions >
TurnHub CI > Run workflow**. A new push cancels an older run for the same PR or
branch. A PR run tests GitHub's proposed merge with the base branch.

## Checks

Persistent agent branch maintenance is a separate lightweight workflow:
[agent-branches.yml](../../.github/workflows/agent-branches.yml). On master pushes
it fast-forwards idle branches and reports divergent work without overwriting it.
See [Agent workflow](AGENT_WORKFLOW.md). Agent-branch pushes do not run heavy CI;
open a PR targeting master for the existing component-aware validation. Workflow
and maintenance-script changes require CI, not `[skip ci]`.

| Check name | Work performed |
| --- | --- |
| Host tests and contracts | Atlas gameplay, storage and profile-store suites; Sigil OLED, LED, menu, secure-link, pairing and firmware-package suites; adapter audit; generated-response and shared-fixture contract validation; design tokens current (`design/build_tokens.py --check`); portal pack builds (`Atlas/web/build.py --check`); the app's manual asset matches the newest manual; `tools/firmware/thfw.py` tests |
| Firmware (atlas) | Build Atlas; check its firmware descriptor and, with the signing secret, add a signed `atlas.thfw`; render Atlas screen previews |
| Firmware (sigil) | E-ink Sigil; descriptor check, signed `sigil-eink.thfw`, e-ink and OLED screen previews |
| Firmware (sigil-oled) | OLED Sigil; descriptor check and signed `sigil-oled.thfw` |
| Firmware (sigil-spare) | The inert spare image `flash-all` puts on spare boards; never packaged |
| Android build and unit tests | Run JVM unit tests and build the Play release bundle (`.aab`), signed with the upload key when its secrets are set |

The bench and simulation builds (`sigil-epd-bn`, `sigil-epd-gdey`, `sigil-wokwi`)
are not built in CI (since 2026-10-06): nothing ships them, and they compile the same sources as `sigil`.
Build them locally with `pio run -e <env>` when working on them.

The hardware `TestHarness/` target was retired on 2026-10-05. CI no longer
builds it or publishes harness firmware artifacts. Its source is historical and
unsupported; the Atlas and Sigil host suites remain supported.

Host suites use GCC, C++14, AddressSanitizer and UBSan on Ubuntu 24.04, with
assertions enabled. Sanitizer failures fail the check. The firmware jobs use
PlatformIO Core 6.2.0 and the existing project configurations. Some platform and
library versions are still floating in those configurations; this workflow does
not claim reproducible release builds. Each firmware artifact records its commit,
environment and resolved package versions.

Android uses the committed Gradle wrapper and Temurin 25 (LTS). There are no
Gradle daemon JVM criteria: the daemon runs on whatever JDK starts it (Android
Studio's bundled JBR locally), so a sync never downloads a JDK. The pinned
foojay download that the criteria used to carry broke once Java 26 left
support (2026-10-06). Any JDK 17 or newer builds the app. The hosted runner
supplies the Android SDK; after license acceptance, Gradle installs the SDK
components requested by the project.

The workflow uses read-only repository permission and pinned revisions of
official GitHub actions, plus `r0adkll/upload-google-play` for Play. Its
secrets are `TURNHUB_FIRMWARE_SIGNING_KEY`, the firmware signing key
([Firmware Updates](FIRMWARE_UPDATES.md#keys-and-signing)), the Play upload
key and the Play service account below; without them (for example on a fork's
pull request) builds are left unsigned and still pass. It does not flash devices
or modify repository content/settings; its only publishing is master's
Android bundle to Play internal testing.

Releases are a separate workflow,
[`.github/workflows/release.yml`](../../.github/workflows/release.yml), run by
a pushed tag:

| Tag | Builds and signs | Carried over unchanged |
| --- | --- | --- |
| `v0.9.0` | Atlas, the web portal pack, both Sigils | nothing |
| `atlas-v0.7.1` | Atlas and the web portal pack | both Sigil packages |
| `sigil-v0.9.13` | both Sigils (e-ink and OLED) | Atlas and the portal pack |

Each publishes a GitHub Release with the `.thfw` packages and
`turnhub-firmware.json`, the feed the Android app reads from the *latest*
release. Because of that, a one-product release copies the other products'
packages from the previous latest release and lists them in its feed, so the
feed always names every product and the app and Atlas see every available
update. A `atlas-v`/`sigil-v` tag fails if there is no earlier release to
carry from, so publish a plain `v*` release first. The version in a tag is a
release label only; each package's own version comes from `firmware_version.h`
(or `Atlas/web/VERSION`). It needs `contents: write` and fails without the
signing secret. Branch protection is configured
separately in GitHub, not by this file.

## What runs

A pull request runs only the work its changed files can affect. The first job,
**Detect changed components**, diffs the PR against its base branch and turns
the paths into four switches:

| Changed path | Atlas | Sigil | Android | Packaging tool |
| --- | --- | --- | --- | --- |
| `Atlas/` | yes | | | |
| `Sigil/` | | yes | | |
| `shared/` | yes | yes | | |
| `protocol/` | yes | | yes | |
| `Android/` (except the generated `assets/manual.md`), `design/` | | | yes | |
| `tools/firmware/` | yes | yes | | yes |
| `.github/` | yes | yes | yes | yes |
| anything else (docs, KiCad, scripts) | | | | |

- **Atlas** runs the Atlas host suites, the generated-response contract check
  and the `Firmware (atlas)` build and previews.
- **Sigil** runs the Sigil host suites and the three Sigil firmware builds.
- **Android** runs the Android unit tests and release bundle.
- **Packaging tool** runs `tools/firmware/test_thfw.py`.

The quick Python checks (adapter audit, design tokens, portal pack, manual
asset) run on every PR, so a documentation-only PR skips every build and
suite.

The firmware jobs cache PlatformIO's platform, toolchain and libraries per
environment, keyed by the project's `platformio.ini`. Uncached, those downloads
took about 70 s of the 107 s Atlas build step (run 37633088766); the compile
itself always starts clean.

Unaffected work is skipped inside each job, not by skipping the job, so every
check name above still reports success and branch protection's required
checks are satisfied. A skipped job's steps show as skipped in its log.

Pushes to `master` and manual runs (**Run workflow**) always build and test
everything, so master's Android bundle, its Play upload and the firmware
artifacts are produced on every merge exactly as before. If the changed-file
list cannot be read, the PR also builds everything. To force a full PR run,
start the workflow manually on the PR's branch. The tag release workflow is
separate and unaffected.

## Android release bundle

Every run builds `android-release-<run number>-<sha>`, an `app-release.aab`
whose `versionCode` is the workflow run number, so each bundle is newer than
the last and Play accepts it as a fresh upload. Download it from the run's
Artifacts and upload it under **Play Console > Testing > Internal testing >
Create new release**. The package is `com.turnhub.android` with Play App
Signing: Google holds the app signing key, and CI signs with the upload key.

The upload key lives outside the repository (the owner keeps the `.jks` and its
backup with the other private keys). CI reads it from four repository secrets
(**Settings > Secrets and variables > Actions**):

| Secret | Value |
| --- | --- |
| `TURNHUB_UPLOAD_KEYSTORE_BASE64` | The `.jks` file as one base64 line (PowerShell: `[Convert]::ToBase64String([IO.File]::ReadAllBytes("upload.jks"))`) |
| `TURNHUB_UPLOAD_STORE_PASSWORD` | Keystore password |
| `TURNHUB_UPLOAD_KEY_ALIAS` | Key alias, e.g. `upload` |
| `TURNHUB_UPLOAD_KEY_PASSWORD` | Key password |

`Android/app/build.gradle.kts` signs the release variant only when
`TURNHUB_UPLOAD_KEYSTORE` (the decoded file's path) is set; the job deletes
the decoded file when it ends. Without the secrets the bundle is unsigned and
Play refuses it. Android Studio's **Build > Generate Signed App Bundle** with
the same keystore produces an equivalent bundle locally (its `versionCode` is
1 unless `-Pturnhub.versionCode` is given).

## Publishing to Play

A push to `master` also publishes that bundle to Play's **Internal testing**
track as release "CI build <run number>", rolled out to the track's testers at
once (`r0adkll/upload-google-play`, pinned in the workflow). Pull requests
never publish. The step is skipped, with a notice, unless the upload key
secrets above and `PLAY_SERVICE_ACCOUNT_JSON` are set.

`PLAY_SERVICE_ACCOUNT_JSON` is the JSON key of a Google Cloud service account
that Play Console has invited with release permissions for TurnHub
(**Users and permissions > Invite new users**, the service account's email,
app permission "Release to testing tracks"). The owner creates and keeps that
key; CI never prints it. To stop automatic publishing, delete the secret.

Promoting a build beyond Internal testing stays a manual Play Console step.

## Reading results

1. Open the PR and expand its checks, or open the repository's **Actions** tab.
2. A successful check means its commands passed for that run's commit. A failed
   check links to the job and failing step. Open the log before retrying; a source
   defect needs a fix, while a download/service failure may only need a rerun.
3. Open a successful run's **Artifacts** to download per-target firmware or the
   Android release bundle. Android test reports are retained when available, including
   on failed test runs. Artifacts expire after seven days (the Android bundle
   and screen previews after 14).

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
and the check names above, using GitHub Actions as their source. Keep check
names stable when editing the workflow. Do not add path filters that prevent a
required check from reporting. A solo-maintainer setup does not need a mandatory
second person's approval just to require checks.

